//
// Created by Carter Mbotho on 2020-03-25.
//

#ifndef SUIL_COMMON_H
#define SUIL_COMMON_H

#include <suil/logging.h>
#include <suil/http/endpoint.h>
#include <suil/http/middlewares.h>
#include <suil/sql/pgsql.h>
#include <suil/http/cors.h>

#include "gateway.scc.h"

namespace suil::semauusu {

    using Endpoint = http::TcpEndpoint<
            http::mw::Initializer,     /// needed for initializing the application
            http::SystemAttrs,         /// needed for by routes and other middle-wares
            http::JwtAuthorization,    /// needed for authorization
            http::mw::Redis,           /// needed by most routes and auth middleware
            http::mw::EndpointAdmin,   /// needed for administering the endpoint
            sql::mw::Postgres,         /// needed by most routes
            http::mw::JwtSession,      /// needed for provisioning JWT tokens
            http::Cors>;               /// needed for CORS

    define_log_tag(NZM_GATEWAY);
}
#endif //SUIL_COMMON_H
