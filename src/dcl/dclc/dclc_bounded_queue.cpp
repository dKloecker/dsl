//
// Created by Dominic Kloecker on 16/09/2026.
//

#include "dclc_bounded_queue.h"

namespace dcl {
//
void test() {
	struct Test {
		// Test() = delete;
		int test;
	};
	struct Test2{};

	b_spsc_q<Test, 8> q{};
	// bounded_queue<SPSC, int, 8> {};




	// spsc_bq<Test, 8> q{}''
	// spsc_q<>
	// bounded_queue<SPSC<Test, 8>> q{};
	Test el{};
	q.push(el);
	q.try_pop();
	q.pop(el);
	std::vector<Test> testVec;
	const auto it = testVec.begin();
	q.pop_onto(it);
	q.try_pop();
	b_spsc_q<Test> q1(20);
	// b_spsc_q<Test> q2(4);
	b_mpmc_q<Test, 4> q2{};

	// bounded_queue<SPSC>


	// q
	// details::static_byte_storage<Test, 20> s{};
	// details::static_byte_storage<int, 20> r{};
	// auto val = s.raw_.data();
	// auto val2 = &r.raw_[1];
	// auto val3 = val2;
	// s.destroy_at(1);
	// r.emplace_at(1,1);
	//
	// details::bounded_queue<Test, 8> q{};





}

}