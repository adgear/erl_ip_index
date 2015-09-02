#include "patricia.hpp"
#include <cassert>
#include <cstring>
#include <memory>

extern "C" {
#include "indexed_ewah.h"
}

#define DEFAULT_LARGE_LIST_THRESOLD 1000000

typedef uint32_t Ipv4Ip;
typedef uint64_t Ipv4ListId;

typedef PatriciaKey<Ipv4Ip> Ipv4Mask;
typedef Patricia<Ipv4Ip, Ipv4ListId> Ipv4Tree;
typedef PatriciaElem<Ipv4Ip, Ipv4ListId> Ipv4TreeElem;

struct Ipv4List {
    Ipv4List(uint64_t i, unsigned l, const unsigned char *d) {
        id = i;
        length = l;
        data = d;
    }

    uint64_t id;
    unsigned length;
    const unsigned char *data;
};

struct free_delete
{
    void operator()(void* x) {
	indexed_ewah_map *map = static_cast<indexed_ewah_map *>(x);
	ewah_free(map->map);
	delete map;
    }
};

class Ipv4Map {
public:
    Ipv4ListId id;

    Ipv4Map(Ipv4ListId i) : id(i), bitmap(new indexed_ewah_map()), finalized(false) {
	assert(0 == bitmap.get()->map);
	bitmap.get()->map = ewah_new();
    }

    void add_ip(Ipv4Ip ip) {
        if (!finalized) {
            ewah_set(bitmap.get()->map, ip);
        }
    }

    void finalize(void) {
        finalized = true;
        ewah_build_index(bitmap.get());
    }

    bool lookup(Ipv4Ip ip) {
        return indexed_ewah_get(bitmap.get(), ip);
    }

private:
    std::unique_ptr<indexed_ewah_map, free_delete> bitmap;
    bool finalized;
};

class Ipv4Index {
public:
    Ipv4Index(std::vector<Ipv4List>& lists) : Ipv4Index(lists, DEFAULT_LARGE_LIST_THRESOLD) {}

    Ipv4Index(std::vector<Ipv4List>& lists, unsigned large_list_thresold) {
        std::vector<Ipv4TreeElem> tree_elems;

        for (Ipv4List &list : lists) {
            bool large_list = list.length >= large_list_thresold;

            if (large_list) {
                maps.emplace_back(list.id);
            }
            else {
                tree_elems.reserve(tree_elems.size() + list.length / 5);
            }

            for (unsigned i = 0; i < list.length; i += 5) {
                uint32_t ip = (list.data[i+0] << 24) + (list.data[i+1] << 16) + (list.data[i+2] << 8) + list.data[i+3];
                uint8_t mask = list.data[i+4];

                if (large_list && mask == 32) {
                    maps[maps.size()-1].add_ip(ip);
                }
                else {
                    uint8_t offset = 32 - mask;
                    tree_elems.emplace_back(offset, ip, list.id);
                }
            }

            if (large_list) {
                maps[maps.size()-1].finalize();
            }
        }

        tree = Ipv4Tree(tree_elems);
    }

    std::vector<Ipv4ListId> lookup(Ipv4Ip ip, uint8_t mask) {
        std::vector<Ipv4ListId> result = tree.lookup(ip, 32 - mask);

        if (mask == 32) {
            for (Ipv4Map &map : maps) {
                if (map.lookup(ip)) {
                    result.emplace_back(map.id);
                }
            }
        }

        return result;
    }

private:

    Ipv4Tree tree;
    std::vector<Ipv4Map> maps;

};
