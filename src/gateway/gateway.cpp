//
// Created by Carter Mbotho on 2020-03-25.
//

#include <suil/sql/pgsql.h>
#include <suil/mustache.h>
#include "users.h"
#include "gateway.h"

namespace suil::nozama {

    Gateway::UPtr sGateway{nullptr};

    Gateway& Gateway::get()
    {
        if (sGateway == nullptr) {
            sGateway = Gateway::UPtr{new Gateway};
        }
        return *sGateway;
    }

    void Gateway::start(cmdl::Cmd& cmd)
    {
        Gateway::get().initialize(cmd.getvalue("config", String{}), cmd.getvalue("reset", false));
        Gateway::get().install<Users>();
        int code = Gateway::get().run();
        ldebug(&Gateway::get(), "application exiting, %d", code);
    }

    int Gateway::run()
    {
        if (ep == nullptr) {
            throw Exception::create("application not initialized");
        }

        if (mControllers.empty()) {
            iwarn("starting application without route controllers");
        }

        // initialize controllers if any
        for (auto& controller: mControllers) {
            controller.second->init();
        }

#ifdef SWEPT
        if (!utils::fs::exists(".sweep")) {
            int code{0};
            size_t size{sizeof(code)};
            utils::fs::append(".sweep", &code, size);
        }
#endif

        return ep->start();
    }

    void Gateway::initialize(const suil::String &configPath, bool reset)
    {
        // load configuration
        sdebug("intializing gateway {config: %s, reset = %d}", configPath(), reset);
        Ego.mResetRequested = reset;
        Ego.mConfig = json::Object::fromLuaFile(configPath);

        if (Ego.ep != nullptr) {
            throw Exception::create("Gateway already initialized");
        }
        initLogging();
        initEndpoint();
        initPgsql();
        initRedis();
        initJwtAuth();
        initOutbox();
        initAdminEndpoint();

        scoped(conn, ep->middleware<sql::mw::Postgres>().conn());
        Settings settings(conn);
        auto initialized = settings["initialized"] || false;
        if (!initialized) {
            auto& api = *ep;
            api.middleware<http::mw::Initializer>().setup(api,
                               std::bind(&Gateway::firstUse, this, std::placeholders::_1, std::placeholders::_2));
        }
        else {
            Ego.AdminEmail = ((String) settings["admin_email"]).dup();
            // Ego.Frontend = ((String) settings["frontend"]).dup();
        }
    }

    void Gateway::initLogging()
    {
        idebug("initializing gateway logging");
        auto logObj = Ego.mConfig["logging"];
        auto verboseObj = logObj("verbose");
        if (verboseObj) {
            // configure logging verbosity
            auto verbose = (log::Level) (int)verboseObj;
            log::setup(opt(verbose, verbose));
        }
        auto dirObj = logObj("dir");
        if (dirObj) {
            // configure File logging
            auto dir = (std::string) dirObj;
            idebug("Initializing gateway logging to directory %s", dir.c_str());
            mLogger = std::make_unique<FileLogger>(dir, "gateway");
            log::setup(opt(sink, [this](const char *msg, size_t size, log::Level l) {
                if (mLogger != nullptr) {
                    mLogger->log(msg, size, l);
                }
                // also log with default handler
                log::Handler()(msg, size, l);
            }));
        }
    }

    void Gateway::initEndpoint()
    {
        idebug("initializing HTTP endpoint");
        auto httpObj = Ego.mConfig["http"];
        ep = std::make_unique<Endpoint>("",
                          opt(name, (httpObj("server.name") or std::string{"0.0.0.0"})),
                          opt(port, (httpObj("server.port") or 1080)),
                          opt(accept_backlog, (httpObj("server.backlog") or 5000)));
        Ego.Url = (httpObj("url") or String{"localhost"}).dup();
    }

    void Gateway::initAdminEndpoint()
    {
        idebug("initializing AdminEndpoint middleware");
        auto admin = ep->middleware<http::mw::EndpointAdmin>();
        admin.setup(*ep);
    }

    void Gateway::initOutbox()
    {
        idebug("initializing mailer");
        auto mailerObj = Ego.mConfig["mail"];
        auto server = (String) mailerObj("stmp.host", true);
        mOutbox = MailOutbox::mkshared(server,
                (int) mailerObj("stmp.port", true),
                Email::Address{(String) mailerObj("sender.email", true),
                               mailerObj("sender.name") || String{}});

        if (!mOutbox->login(
                (String) mailerObj("stmp.username", true),
                (String) mailerObj("stmp.passwd", true)))
        {
            throw Exception::create("Logging into STMP server failed");
        }

        itrace("Logged in to STMP server %s", server());
    }

    void Gateway::initPgsql()
    {
        idebug("initializing postgres database middleware");
        auto& pq = ep->middleware<sql::mw::Postgres>();

        auto postgresObj = Ego.mConfig["postgres"];
        auto fromEnv = postgresObj("connect.env") or String{};
        String connStr{};
        if (fromEnv) {
            // connect using environment variable
            connStr = utils::env(fromEnv(), String{});
            if (connStr == nullptr) {
                // cannot continue without connection parameters
                throw Exception::create(
                        "Postgres connection string not found in environment variable ", fromEnv);
            }
        }
        else {
            // build connection string from variables
            OBuffer ob{128};
            auto dbName   = (String) postgresObj("connect.dbname", true);
            auto dbUser   = (String) postgresObj("connect.user", true);
            auto dbPasswd = (String) postgresObj("connect.passwd", true);
            auto dbHost   = postgresObj("connect.host") || String{"localhost"};
            auto dbPort   = postgresObj("connect.port") || 0;

            ob << "host=" << dbHost << " user=" << dbUser << " password="
               << dbPasswd << " dbname=" << dbName;
            if (dbPort) ob << " port=" << dbPort;
            connStr = String(ob);
        }
        pq.setup(connStr(),
                 opt(ASYNC, true),
                 opt(TIMEOUT, postgresObj("timeout")   || -1),
                 opt(EXPIRES, postgresObj("keepAlive") || -1));

        /* initialize schemas */
        scoped(conn, pq.conn());

        /* initialize in transaction block, changes will be reverted on failure */
        {
            sql::PgSqlTransaction txn(conn);
            try {
                /* initialize settings */
                Settings(conn).init(Ego.mResetRequested);
                /* create users */
                Users::Table users(conn);
                if (users.cifne(Ego.mResetRequested)) {
                    /* add default user */
                }
            }
            catch (...) {
                // abort by rolling back changes on the transaction
                txn.rollback();
                throw;
            }
        }

        itrace("postgres database middleware initialized");
    }

    void Gateway::initRedis()
    {
        idebug("initializing redis database middleware");
        auto& redis   = ep->middleware<http::mw::Redis>();
        auto redisObj = Ego.mConfig("redis.*", true);
        auto host     = (String) redisObj("connect.host", true);
        auto port     = redisObj("connect.port") || 6379;
        auto password = redisObj("connect.passwd") || String{};
        auto keepAlive = redisObj("keepAlive") || uint64_t(0);

        redis.setup(host(), port,
                    opt(passwd, std::move(password)),
                    opt(keep_alive, keepAlive));
        if (Ego.mResetRequested) {
            scoped(conn, redis.conn(1));
            auto keys = conn.keys("*");
            for (auto &key: keys) {
                // delete all the other keys
                conn.del(key);
            }
        }
        itrace("redis database module initialized");
    }

    void Gateway::initJwtAuth()
    {
        idebug("initializing JWT auth middleware");
        auto& jwt = ep->middleware<http::JwtAuthorization>();
        auto  jwtObj = Ego.mConfig("jwt.*", true);
        jwt.setup(
                opt(expires, jwtObj["expires"] || 900),
                opt(key,     jwtObj["key"]     || String{}),
                opt(realm,   jwtObj["realm"]   || String{}),
                opt(domain,  jwtObj["domain"]  || String{}),
                opt(path,    jwtObj["path"]    || String{}));

        itrace("JWT authorization middleware initialized");
    }

    bool Gateway::sendTemplatedEmail(
            const String &dst,
            const String &subject,
            const String &mustachePath,
            const json::Object &params)
    {
        if (auto outbox = Outbox().lock()) {
            auto msg = outbox->draft(dst, subject);
            auto &tmpl = MustacheCache::get().load(mustachePath);
            tmpl.render(msg->body(), params);
            msg->content("text/html");
            outbox->send(std::move(msg));
            return true;
        }
        else {
            return false;
        }
    }

    bool Gateway::firstUse(const suil::http::Request &req, suil::http::Response &resp)
    {
        resp.setContentType("application/json");
        InitRequest initRequest;
        try {
            initRequest = req.toJson<InitRequest>();
            // give this user administration role
            initRequest.Administrator.Roles.push_back(http::mw::EndpointAdmin::Role);
            Ego.Controller<Users>().registerUser(req, resp, initRequest.Administrator);
            if (!resp.Ok(http::Status::CREATED)) {
                return false;
            }
        }
        catch (...) {
            ierror("/app-init %s", Exception::fromCurrent().what());
            resp.clear();
            Endpoint::Controller::fail(resp, "BadRequest",
                             "Processing register request failed, contact system administrator");
            resp.end(http::Status::BAD_REQUEST);
            return false;
        }

        scoped(conn, ep->middleware<sql::mw::Postgres>().conn());
        sql::PgSqlTransaction txn(conn);

        try {
            // activate a user account
            bool active =
                    conn("UPDATE users SET State = $1, Notes = $2 WHERE Email = $3")
                            ((int) Users::Active, "", initRequest.Administrator.Email).status();
            if (!active) {
                throw Exception::create("Activating administrator account failed");
            }

            // modify application settings
            auto settings = Settings(conn);
            settings.set("initialized", true);
            settings.set("admin_email", initRequest.Administrator.Email);

            resp.clear();
            resp << "Application successfully initialized"
                 << "\nDisregard the email to verify account";
            resp.end(http::Status::OK);
            return true;
        }
        catch (...) {
            ierror("/app-init %s", Exception::fromCurrent().what());
            resp.clear();
            Endpoint::Controller::fail(resp, "InternalError",
                             "Processing register request failed, contact system administrator");
            resp.end(http::Status::INTERNAL_ERROR);
            txn.rollback();
        }

        try {
            scoped(conn2, ep->middleware<sql::mw::Postgres>().conn());
            // try removing created user
            if (initRequest.Administrator.Email) {
                conn2("DELETE FROM users WHERE Email=$1")(initRequest.Administrator.Email);
            }
        }
        catch (...) {
            ierror("/app-init %s", Exception::fromCurrent().what());
        }

        return false;
    }
}

