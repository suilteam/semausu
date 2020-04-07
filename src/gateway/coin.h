//
// Created by Carter Mbotho on 2020-04-06.
//

#ifndef SUIL_COIN_H
#define SUIL_COIN_H

#include "common.h"

namespace suil::semausu {

    /**
     * @class Coin is the gateway to bank account APIs implemented in block chain
     */
    struct Coin final : Endpoint::Controller, LOGGER(NZM_GATEWAY) {
        using Base = typename Endpoint::Controller;

    private:
        [[method("GET")]]
        [[desc("")]]
        void getBalance(const http::Request& req, http::Response& resp);

        [[method("POST")]]
        [[desc("")]]
        void openAccount(const http::Request& req, http::Response& resp);

        [[method("PUT")]]
        [[desc("")]]
        void depositAmount(const http::Request& req, http::Response& resp);

        [[method("POST")]]
        [[desc("")]]
        void transferAmount(const http::Request& req, http::Response& resp);

        [[method("POST")]]
        [[desc("")]]
        void payMerchant(const http::Request& req, http::Response& resp);
    };

}
#endif //SUIL_COIN_H
