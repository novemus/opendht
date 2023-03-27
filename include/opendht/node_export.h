/*
 *  Copyright (C) 2014-2023 Savoir-faire Linux Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include "def.h"
#include "infohash.h"
#include <asio/ip/udp.hpp>

#include <string_view>

#include <string_view>

namespace dht {
using namespace std::literals;

struct OPENDHT_PUBLIC NodeExport {
    InfoHash id;
    asio::ip::udp::endpoint addr;

    template <typename Packer>
    void msgpack_pack(Packer& pk) const
    {
        pk.pack_map(2);
        pk.pack("id"sv);
        pk.pack(id);
        pk.pack("addr"sv);
        pk.pack_bin(addr.size());
        pk.pack_bin_body((const char*)addr.data(), addr.size());
    }

    void msgpack_unpack(msgpack::object o);

    OPENDHT_PUBLIC friend std::ostream& operator<< (std::ostream& s, const NodeExport& h);
};

}
