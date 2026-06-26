//
// Created by Adminstudio on 6/25/2026.
//

#ifndef BABOMATCHINGENGINE_FASTMAP_H
#define BABOMATCHINGENGINE_FASTMAP_H

#include <functional>
#include <map>
#include <utility>

namespace babo {


template<class Key, class T, class Compare = std::less<Key>>
class multimap {
public:
    multimap(){}

    // insert();
    // emplace;
    // erase(iterator);
    //
    // find(key);
    // begin(); end(); rbegin(); rend();
    //
    // iterator(first, second, ++pre, post++, --i, i--, ==, != end)

private:
    std::map<Key, T, Compare> map_;
};

}
#endif // BABOMATCHINGENGINE_FASTMAP_H
