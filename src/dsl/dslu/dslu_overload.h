//
// Created by Dominic Kloecker on 16/04/2026.
//

#ifndef DSL_DSLU_OVERLOAD_H_
#define DSL_DSLU_OVERLOAD_H_

namespace dsl {
// Overloaded Visitor for variants, based on https://stackoverflow.com/questions/66961406/c-variant-visit-overloaded-function
template<typename... Overloads>
struct overload : Overloads... {
    using Overloads::operator()...;
};

template<typename... Overloads>
overload(Overloads...) -> overload<Overloads...>;
}

#endif  // DSL_DSLU_OVERLOAD_H_
