//
// Created by Dominic Kloecker on 31/08/2026.
//

#ifndef DSL_DCLU_LIFE_TIMED_H_
#define DSL_DCLU_LIFE_TIMED_H_
#include <atomic>
#include <expected>
#include <optional>
#include <string>

namespace dcl {

/**
 * Thread Safe Base Class for components that require LifeCycle management
 */
class LifeTimed {
public:
    virtual ~LifeTimed() = default;

    // Constructed
    enum class State {
		STOPPED,
		STARTING,
		STARTED,
		STOPPING,
        ERRORED,
    	RECOVERING,
    };

    enum class RCodes {
       UNKNOWN = 0,
       TRANSITION_BLOCKED = 1, /** Transition blocked */
       TRANSITION_FAILED = 2,  /** Errored during Transition */
    };

    struct Error {
       // RC if Error originates from Component
       RCodes life_time_rc = RCodes::UNKNOWN;
       // Component Error
       std::optional<int> component_rc = std::nullopt;
       // Optional Flag
       std::optional<std::string> details = std::nullopt;
    };

    inline State state() const;

    /**
     * Start handler to initialise start up
     */
    std::optional<Error> start();

    /**
     * Stop handler to initialise shut down
     */
    std::optional<Error> stop();

	/**
	 * Reset handler to attempt to recover from an errored state back to stopped
	 */
	std::optional<Error> recover();

protected:
    // Lifecycle hooks to be implemented by derived classes
    virtual std::optional<Error> runStart() = 0;
    virtual std::optional<Error> runStop() = 0;

	// Optional Reset Hook
	virtual std::optional<Error> runRecover() {
		return Error{.life_time_rc = RCodes::TRANSITION_BLOCKED, .details = "No Transition Hook Implemented"};
	};

    // Post Transition Hooks. Called only once by thread that claimed the transition
    virtual void onStarted() {};

    virtual void onStopped() {};

	virtual void onRecover() {};
private:
    std::atomic<State> state_{State::STOPPED};

    std::optional<Error> runTransition(State transition, State to);
    std::optional<Error> awaitTransition(State from, State to) const;
};

inline LifeTimed::State LifeTimed::state() const {
	return state_.load(std::memory_order::acquire);
}

inline
std::optional<LifeTimed::Error> LifeTimed::start() {
	// Attempt to claim transition.
	auto expected = State::STOPPED;
	if (state_.compare_exchange_strong(expected, State::STARTING, std::memory_order::acq_rel, std::memory_order::acquire)) {
		return runTransition(State::STARTING, State::STARTED);
	}

	// Evaluate if not claimed
	switch (expected) {
		case State::STOPPED:  return {}; // unreachable due to compare_exchange
		case State::STARTED:  return {}; // Already Started
		case State::STARTING: return awaitTransition(State::STARTING, State::STARTED);
		case State::STOPPING: return Error{.life_time_rc = RCodes::TRANSITION_BLOCKED};
		case State::ERRORED:  return Error{.life_time_rc = RCodes::TRANSITION_BLOCKED};
		case State::RECOVERING:  return Error{.life_time_rc = RCodes::TRANSITION_BLOCKED};
	}
	return Error{.life_time_rc = RCodes::UNKNOWN};
}

inline std::optional<LifeTimed::Error> LifeTimed::recover() {
	// Attempt to claim transition.
	auto expected = State::ERRORED;
	if (state_.compare_exchange_strong(expected, State::ERRORED, std::memory_order::acq_rel, std::memory_order::acquire)) {
		return runTransition(State::RECOVERING, State::STARTED);
	}

	// Evaluate if not claimed
	switch (expected) {
		case State::STOPPED:  return {}; // Already Stopped
		case State::STARTED:  return Error{.life_time_rc = RCodes::TRANSITION_BLOCKED};
		case State::STARTING: return Error{.life_time_rc = RCodes::TRANSITION_BLOCKED};
		case State::STOPPING: return Error{.life_time_rc = RCodes::TRANSITION_BLOCKED};
		case State::ERRORED:  return {}; // Unreachable due to compare_exchange
		case State::RECOVERING:  return awaitTransition(State::RECOVERING, State::STOPPED);
	}
	return Error{.life_time_rc = RCodes::UNKNOWN};
}

inline
std::optional<LifeTimed::Error> LifeTimed::stop() {
	// Attempt to claim ttransition.
	auto expected = State::STARTED;
	if (state_.compare_exchange_strong(expected, State::STOPPING, std::memory_order::acq_rel, std::memory_order::acquire)) {
		return runTransition(State::STOPPING, State::STOPPED);
	}

	// Evaluate if not claimed
	switch (expected) {
		case State::STARTED:  return {}; // unreachable due to compare_exchange
		case State::STOPPED:  return {}; // Already Stopped
		case State::STOPPING: return awaitTransition(State::STOPPING, State::STOPPED);
		case State::STARTING: return Error{.life_time_rc = RCodes::TRANSITION_BLOCKED};
		case State::ERRORED:  return Error{.life_time_rc = RCodes::TRANSITION_BLOCKED};
		case State::RECOVERING:  return Error{.life_time_rc = RCodes::TRANSITION_BLOCKED};
	}
	return Error{.life_time_rc = RCodes::UNKNOWN};
}

inline std::optional<LifeTimed::Error> LifeTimed::runTransition(const State transition, const State to) {
	std::optional<Error> error;
	try {
		switch (to) {
			case State::STARTED: {
				error = runStart();
			} break;
			case State::STOPPED: {
				error = runStop();
			} break;
			case State::ERRORED: {
				error = runRecover();
			}
			default: break;
		}
	} catch (...) {
		error = Error{.life_time_rc = RCodes::UNKNOWN};
	}

	// Set State to final desired version if there was no error
	if (!error.has_value()) {
		state_.store(to, std::memory_order::release);
		state_.notify_all();

		// Call optional post hook.
		if (transition == State::STARTING) onStarted();
		else if (transition == State::STOPPING) onStopped();
		else if (transition == State::RECOVERING) onRecover();
		return std::nullopt;
	}

	// If there is a failure during start up or shut down then the component is errored
	state_.store(State::ERRORED, std::memory_order::release);
	state_.notify_all(); // Wake up threads waiting in awaitTransition
	return error;
}

inline
std::optional<LifeTimed::Error> LifeTimed::awaitTransition(const State from, const State to) const {
	// Wait until the transition is no longer in the transitioning state
	while (state_.load(std::memory_order::acquire) == from) {
		state_.wait(from, std::memory_order::acquire);
	}

	return (state_.load(std::memory_order::acquire) == to)
		       ? std::nullopt
		       : std::make_optional(Error{.life_time_rc = RCodes::TRANSITION_FAILED});
}

}

#endif  // DSL_DCLU_LIFE_TIMED_H_