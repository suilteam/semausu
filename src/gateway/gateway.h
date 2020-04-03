//
// Created by Carter Mbotho on 2020-03-25.
//

#ifndef SUIL_GATEWAY_H
#define SUIL_GATEWAY_H

#include <suil/email.h>
#include <suil/cmdl.h>

#include <typeindex>

#include "common.h"

namespace suil::nozama {

    struct Gateway final: LOGGER(NZM_GATEWAY) {
#ifndef SWEPT
        using MailOutbox = SslMailOutbox;
#else
        using MailOutbox = TcpMailOutbox;
#endif
        sptr(Gateway);

        static Gateway& get();

        static void start(cmdl::Cmd& cmd);

    public:
        String AdminEmail;
        String PasswdKey;
        String Url;

        json::Object& Config() { return mConfig; }
        MailOutbox::WPtr Outbox() { return mOutbox; }

        template <typename C>
        C& Controller() {
            auto it = mControllers.find(std::type_index(typeid(C)));
            if (it == mControllers.end()) {
                throw Exception::create("Gateway does not contain controller '", typeid(C).name(), "'");
            }
            return dynamic_cast<C&>(*(it->second.get()));
        }

    private:
        void initialize(const String& configPath, bool reset);
        int  run();

    private:
        Gateway() = default;

        template <typename C, typename...Args>
        void install(Args... args) {
            static_assert(std::is_base_of_v<Endpoint::Controller, C>, "Only controllers can be installed");
            auto it = mControllers.find(std::type_index(typeid(C)));
            if (it != mControllers.end()) {
                throw Exception::create("Controller '", typeid(C).name(), "' already installed");
            }
            mControllers.emplace(std::type_index(typeid(C)),
                                 Endpoint::Controller::UPtr{ new C(api(), std::forward<Args>(args)...) });

        }

        void initEndpoint();
        void initAdminEndpoint();
        void initOutbox();
        void initPgsql();
        void initJwtAuth();
        void initRedis();
        void initLogging();

        /**
         * first use handler will be invoked when the user installs the application
         * @param req
         * @param resp
         */
        bool firstUse(const http::Request& req, http::Response& resp);

    private:
        using ControllerBox = std::unordered_map<std::type_index, Endpoint::Controller::UPtr>;
        Endpoint& api() { return *ep; }
        Endpoint::unique_ptr ep;
        MailOutbox::Ptr   mOutbox;
        ControllerBox        mControllers;
        json::Object         mConfig;
        bool                 mResetRequested{false};
        std::unique_ptr<FileLogger> mLogger{nullptr};
    };
}
#endif //SUIL_GATEWAY_H
