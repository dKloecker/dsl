//
// Created by Dominic Kloecker on 22/06/2026.
//

#ifndef DSL_STARTABLE_H
#define DSL_STARTABLE_H

#include <atomic>
#include <cassert>
#include <exception>
#include <mutex>
#include <expected>
#include <concepts>

namespace dsl::lifecycle {

enum class State {
	STOPPED,
	STARTING,
	STARTED,
	STOPPING,
	ERRORED,
	RESTARTING
};

enum class ErrorCode {
	UNKNOWN,
	TRANSITION_BLOCKED, /** Requester Transition not permitted due to source LifeTimeState */
	FAILED_TRANSITION,  /** Transition failed */
	LIFECYCLE_FAILED,   /** LifeTimeError during post-transition hook */
	IS_ERRORED,         /** Component is Errored */
};

class Error {
public:
	ErrorCode                 rc      = ErrorCode::UNKNOWN;
	std::optional<std::string> details = std::nullopt;
};

template<typename S>
concept LifeTimed = requires(S &s, Error rc) {
	/**
	 * @brief User-defined start handler.
	 * Must complete every step required for the component to be considered `STARTED`.
	 * Guaranteed to execute on exactly one thread, during the `STARTING` LifeTimeState.
	 * Returning an error (or throwing) routes through `on_error` and prevents the `STARTED` transition.
	 */
	{ s.on_start() } -> std::convertible_to<std::expected<void, Error> >;

	/**
	 * @brief User-defined stop handler.
	 * Must complete every step required for the component to be considered `STOPPED`.
	 * Guaranteed to execute on exactly one thread, during the `STOPPING` LifeTimeState.
	 * Returning an error (or throwing) routes through `on_error`.
	 */
	{ s.on_stop() } -> std::convertible_to<std::expected<void, Error> >;

	/**
     * @brief Optional handler for how to handle transition change errors
     * @return void if the error is handled, and we are in a safe (Stopped) LifeTimeState, LifeTimeError if we are in the invalid LifeTimeState.
     */
	{ s.on_transition_error(rc) } -> std::convertible_to<std::expected<void, Error> >;
};

template<typename S>
concept Restartable = requires(S &s) {
	requires LifeTimed<S>;
	{ s.on_restart() } -> std::convertible_to<std::expected<void, Error> >;
};

/**
 * @brief Thread-Safe Base class for components with Start / Stop Lifecycle.
 *
 * A `Startable` owns the lifecycle of the internal LifeTimeState machine.
 * Any derived component must only implement the transition requirements via `on_start` and `on_stop`.
 * Optionally can implement an `on_error` error handler to determine if the LifeTimeState is recoverable or not.
 *
 * The following Transitions are Permitted
 * @code
 * STOPPED   -- start()   --> STARTING  -- on_start()	[Success] --> STARTED
 *						      STARTING  -- on_start()	[Failed]  --> ERRORED | STOPPED
 * STARTED   -- stop()    --> STOPPING  -- on_stop()	[Success] --> STOPPED
 *						      STOPPING  -- on_stop()	[Failed]  --> ERRORED | STOPPED
 * ERRORED   -- restart() --> RESETTING -- on_restart() [Success] --> STOPPED
 *							  RESETTING -- on_restart() [Failed]  --> ERRORED
 * @endcode
 *
 * If multiple threads request the same transition simultaneously, a single thread will take ownership of the
 * life cycle operations, while the remaining threads will be blocked until the transition has completed or failed.
 *
 * Any transition will publish a Terminal Lifetime LifeTimeState towards its completion, thereby releasing waiting threads.
 */
template<typename Derived>
class Startable {
public:
	virtual ~Startable() = default;

	/**
     * @brief Thread-safe start
     * @code
     * [tw] (if STOPPED)   on_start(): STOPPED -> STARTING (ownership of start)
     * [tr] (if STARTED)   No-op
     * [tr] (if STARTING)  Wait for the starting thread, then report the outcome
     * [tr] (if STOPPING)  Report failure (TRANSITION_BLOCKED)
     * [tr] (if RESETTING) Report failure (TRANSITION_BLOCKED)
     * [tr] (if ERRORED)   Report failure (IS_ERRORED)
     * @endcode
     *
     * @return Empty on success, or an `LifeTimeError` describing why start could not complete.
     */
	std::expected<void, Error> start() requires LifeTimed<Derived> {
		auto expected = State::STOPPED;
		if (LifeTimeState_.compare_exchange_strong(expected,
		                                   State::STARTING,
		                                   std::memory_order_acq_rel,
		                                   std::memory_order_acquire)) {
			return run_startup();
		}

		switch (expected) {
			case State::STARTED: return {};
			case State::STARTING: return await_transition(State::STARTING, State::STARTED);
			case State::STOPPING: [[fallthrough]];
			case State::RESTARTING: return std::unexpected{Error{.rc = ErrorCode::TRANSITION_BLOCKED}};
			case State::ERRORED: return std::unexpected{Error{.rc = ErrorCode::IS_ERRORED}};
			case State::STOPPED: break; //unreachable
		}
		return std::unexpected{Error{.rc = ErrorCode::UNKNOWN}};
	}

	/**
     * @brief Thread-safe stop
     * @code
     * [tw] (if STARTED)   on_stop(): STARTED -> STOPPING (ownership of stop)
     * [tr] (if STOPPED)   No-op
     * [tr] (if STOPPING)  Wait for the stopping thread, then report the outcome
     * [tr] (if STARTING)  Report failure (TRANSITION_BLOCKED)
     * [tr] (if RESETTING) Report failure (TRANSITION_BLOCKED)
     * [tr] (if ERRORED)   Report failure (IS_ERRORED)
     * @endcode
     */
	std::expected<void, Error> stop() requires LifeTimed<Derived> {
		auto expected = State::STARTED;
		if (LifeTimeState_.compare_exchange_strong(expected,
		                                   State::STOPPING,
		                                   std::memory_order_acq_rel,
		                                   std::memory_order_acquire)) {
			return run_shutdown();
		}

		switch (expected) {
			case State::STOPPED: return {};
			case State::STOPPING: return await_transition(State::STOPPING, State::STOPPED);
			case State::STARTING: [[fallthrough]];
			case State::RESTARTING: return std::unexpected{Error{.rc = ErrorCode::TRANSITION_BLOCKED}};
			case State::ERRORED: return std::unexpected{Error{.rc = ErrorCode::IS_ERRORED}};
			case State::STARTED: break; // unreachable
		}
		return std::unexpected{Error{.rc = ErrorCode::UNKNOWN}};
	}

	/**
     * @brief Thread-safe restart of startable to recover from ERRORED LifeTimeState
     * @code
     * [tw] (if ERRORED)   ERRORED -> STOPPED: on_restart();
     * [tr] (if STOPPED)   No-op
     * [tr] (if RESETTING) Wait and report the result
     * [tr] (if STARTED)   Report failure (TRANSITION_BLOCKED)
     * [tr] (if STOPPING)  Report failure (TRANSITION_BLOCKED)
     * [tr] (if STARTING)  Report failure (TRANSITION_BLOCKED)
     * @endcode
     */
	std::expected<void, Error> restart() requires Restartable<Derived> {
		auto expected = State::ERRORED;
		if (LifeTimeState_.compare_exchange_strong(expected,
		                                   State::RESTARTING,
		                                   std::memory_order_acq_rel,
		                                   std::memory_order_acquire)) {
			return attempt_restart();
		}

		switch (expected) {
			case State::STOPPED: return {};
			case State::RESTARTING: return await_transition(State::RESTARTING, State::STOPPED);
			case State::STOPPING: [[fallthrough]];
			case State::STARTING: [[fallthrough]];
			case State::STARTED: return std::unexpected{Error{.rc = ErrorCode::TRANSITION_BLOCKED}};
			case State::ERRORED: break; // unreachable
		}
		return std::unexpected{Error{.rc = ErrorCode::UNKNOWN}};
	}

	/**
     * Block during an active transition e.g. `STARTING`, until the transition function reaches a terminal LifeTimeState
     * and alerts the waiting thread.
     */
	[[nodiscard]] std::expected<void, Error> await_transition(const State transitional, const State success) const {
		while (LifeTimeState_.load(std::memory_order_acquire) == transitional) {
			LifeTimeState_.wait(transitional, std::memory_order_acquire);
		}
		return (LifeTimeState_.load(std::memory_order_acquire) == success)
			       ? std::expected<void, Error>{}
			       : std::unexpected{Error{.rc = ErrorCode::FAILED_TRANSITION}};
	}

	[[nodiscard]] State state() const { return LifeTimeState_.load(std::memory_order_acquire); };

	virtual std::expected<void, Error> on_transition_error(const Error &error) {
		return std::unexpected{error};
	}

private:
	std::expected<void, Error> run_startup() {
		assert(LifeTimeState_.load(std::memory_order_acquire) == State::STARTING);
		return run_transition(State::STARTED,
		                      [this]() -> std::expected<void, Error> {
			                      auto res = static_cast<Derived *>(this)->on_start();
			                      if (res) return {};
			                      return std::unexpected{
				                      Error{.rc = ErrorCode::FAILED_TRANSITION, .details = res.error().details}
			                      };
		                      });
	}

	std::expected<void, Error> run_shutdown() {
		assert(LifeTimeState_.load(std::memory_order_acquire) == State::STOPPING);
		return run_transition(State::STOPPED,
		                      [this]() -> std::expected<void, Error> {
			                      auto res = static_cast<Derived *>(this)->on_stop();
			                      if (res) return {};
			                      return std::unexpected{
				                      Error{.rc = ErrorCode::FAILED_TRANSITION, .details = res.error().details}
			                      };
		                      });
	}

	std::expected<void, Error> attempt_restart() requires Restartable<Derived> {
		assert(LifeTimeState_.load(std::memory_order_acquire) == State::RESTARTING);
		return run_transition(State::STOPPED,
		                      [this]() -> std::expected<void, Error> {
			                      auto res = static_cast<Derived *>(this)->on_restart();
			                      if (res) return {};
			                      return std::unexpected{
				                      Error{.rc = ErrorCode::FAILED_TRANSITION, .details = res.error().details}
			                      };
		                      });
	}

	template<typename F>
	std::expected<void, Error> run_transition(const State final, F &&transition_fn) {
		std::expected<void, Error> result;
		try {
			result = transition_fn();
		} catch (const std::exception &err) {
			result = std::unexpected{Error{.rc = ErrorCode::FAILED_TRANSITION, .details = err.what()}};
		} catch (...) {
			result = std::unexpected{Error{.rc = ErrorCode::FAILED_TRANSITION}};
		}


		if (result) {
			LifeTimeState_.store(final, std::memory_order_release);
			LifeTimeState_.notify_all();
			return {};
		}

		const auto handled_result = on_transition_error(result.error());
		LifeTimeState_.store((handled_result) ? State::STOPPED : State::ERRORED, std::memory_order_release);
		LifeTimeState_.notify_all();
		return result; // Return original failure
	}

	std::atomic<State> LifeTimeState_ = State::STOPPED;
};

}

#endif //DSL_STARTABLE_H
