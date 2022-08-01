#ifndef __DUMMY_PRODUCT_H
#define __DUMMY_PRODUCT_H

#include <vector>
#include <boost/serialization/vector.hpp>
#include <string>
#include <boost/serialization/string.hpp>

struct dummy_product {
    std::string data;

    template<typename A>
    void serialize(A& ar, const unsigned int version) {
        ar & data;
    }

};

#endif
