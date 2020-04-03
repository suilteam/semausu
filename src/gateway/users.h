//
// Created by Carter Mbotho on 2020-03-25.
//

#ifndef SUIL_USERS_H
#define SUIL_USERS_H

#include "common.h"

namespace suil::nozama {

    struct Users final : Endpoint::Controller, LOGGER(NZM_GATEWAY) {
        using Base = typename Endpoint::Controller;

        struct Table : sql::PgsqlMetaOrm<User> {
            static constexpr const char* TABLE = "users";
            Table(sql::PgSqlConnection& conn)
                    : sql::PgsqlMetaOrm<User>(TABLE, conn)
            {}
        };

        enum State : int {
            Blocked,    /// User currently blocked
            Verify,     /// User has to verify their account
            Active      /// User is currently active
        };

        Users(Endpoint& ep);

        void init();

    private:
        friend struct Gateway;
        [[method("POST")]]
        [[desc("Registers a user with semausu's gateway")]]
        void registerUser_(const http::Request& req, http::Response& resp);

        void registerUser(const http::Request& req, http::Response& resp, User& user);

        [[method("POST")]]
        [[desc("Login a user into semausu system")]]
        void loginUser(const http::Request& req, http::Response& resp);

        [[method("GET")]]
        [[desc("Verifies a user account that was registered")]]
        void verifyUser(const http::Request& req, http::Response& resp);

        [[method("DELETE")]]
        [[desc("Log a user out of semausu")]]
        void logoutUser(const http::Request& req, http::Response& resp);

        [[method("POST")]]
        [[desc("Blocks a user using using the system")]]
        void blockUser(const http::Request& req, http::Response& resp);

        [[method("POST")]]
        [[desc("Changes a user password")]]
        void changePasswd(const http::Request& req, http::Response& resp);
#ifdef SWEPT
        /*
         * The following list of routes are available on swept builds only
         * because they AID in debug
         */
#endif
    };
}
#endif //SUIL_USERS_H
