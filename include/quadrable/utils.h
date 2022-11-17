#pragma once

#include <ostream>
#include <stdexcept>


namespace quadrable {


inline void buildString(std::ostream&) { }

template<class First, class... Rest>
inline void buildString(std::ostream& o, const First& value, const Rest&... rest) {
    o << value;
    buildString(o, rest...);
}

template<class... T>
inline std::runtime_error quaderr(const T&... value) {
    std::ostringstream o;
    buildString(o, value...);
    return std::runtime_error(o.str());
}


}
