//
// Created by Carter Mbotho on 2020-03-25.
//
#include <suil/mustache.h>
#include <suil/http/validators.h>

#include "users.h"
#include "gateway.h"

namespace suil::nozama {

    Users::Users(suil::nozama::Endpoint &ep)
        : Base(ep)
    {}

    void Users::init()
    {
        eproute(api, "/users/register")
        ("POST"_method, "OPTIONS"_method)
        .attrs(opt(PARSE_FORM, true))
        (std::bind(&Users::registerUser_, this, std::placeholders::_1, std::placeholders::_2));

        eproute(api, "/users/login")
        ("POST"_method, "OPTIONS"_method)
        .attrs(opt(PARSE_FORM, true))
        (std::bind(&Users::loginUser, this, std::placeholders::_1, std::placeholders::_2));

        eproute(api, "/users/verify")
        ("POST"_method)
        (std::bind(&Users::verifyUser, this, std::placeholders::_1, std::placeholders::_2));

        eproute(api, "/users/logout")
        ("DELETE"_method)
        (std::bind(&Users::logoutUser, this, std::placeholders::_1, std::placeholders::_2));

        eproute(api, "/users/block")
        ("POST"_method)
        .attrs((opt(AUTHORIZE, Auth{http::mw::EndpointAdmin::Role})))
        (std::bind(&Users::blockUser, this, std::placeholders::_1, std::placeholders::_2));

        eproute(api, "/users/changepasswd")
        ("POST"_method)
        (std::bind(&Users::changePasswd, this, std::placeholders::_1, std::placeholders::_2));
    }

    void Users::registerUser_(const http::Request &req, http::Response &resp)
    {
        try {
            http::RequestForm requestForm(req, {"FirstName", "LastName", "Email", "Passwd"}, ", ");
            User      user;
            auto why = requestForm >> user;
            if (why) {
                /* missing parameters*/
                Base::fail(resp, "MissingFields", why);
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            registerUser(req, resp, user);
        }
        catch(...) {
            /* unhandled error */
            ierror("/users/register %s", Exception::fromCurrent().what());
            Base::fail(resp, "InternalError",
                             "Processing register request failed, contact system administrator");
            resp.end(http::Status::INTERNAL_ERROR);
        }
    }

    void Users::registerUser(const suil::http::Request &req, suil::http::Response &resp, User &user)
    {
        static http::validators::Email EmailValidator;
        static http::validators::Password PasswdValidator;

        try {
            if (!EmailValidator(user.Email)) {
                /* invalid user email address */
                Base::fail(resp, "InvalidEmailAddress", "email address '", user.Email, "' is invalid");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            OBuffer tmp;
            if (!PasswdValidator(tmp, user.Passwd)) {
                /* invalid user email address */
                Base::fail(resp, "InvalidPassword", String{tmp});
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            scoped(conn,  api.template middleware<sql::mw::Postgres>().conn());
            int found{0};
            conn("SELECT COUNT(*) FROM users WHERE email like $1")(user.Email) >> found;
            if (found) {
                /* user already registered */
                Base::fail(resp, "UserAlreadyRegistered", "User with email '", user.Email, "' already registered");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            /* initialize user entities, verification guid */
            user.Notes         = utils::uuidstr();
            user.State         = State::Verify;
            user.Salt          = http::rand_8byte_salt()(user.Email);
            user.Passwd        = http::pbkdf2_sha1_hash(Gateway::get().PasswdKey())(user.Passwd, user.Salt);
            user.PasswdExpires = time(nullptr) + 7776000;

            Table users(conn);
            if (!users.insert(user)) {
                /* adding user to database failed */
                ierror("failed to add user '%s' to database", user.Email);
                Base::fail(resp, "UserRegisterFailure", "Registering user '",
                           user.Email, "' failed, contact system admin");
                resp.end(http::Status::INTERNAL_ERROR);
                return;
            }

            if (auto outbox = Gateway::get().Outbox().lock()) {
                auto msg = outbox->draft(user.Email, "Account successfully Registered");
                auto &tmpl = MustacheCache::get().load("_verify_account.html");
                tmpl.render(msg->body(),
                            json::Object(json::Obj,
                                         "name",     user.FirstName.peek(),
                                         "endpoint", Gateway::get().Url.peek(),
                                         "token",    utils::urlencode(user.Notes),
                                         "email",    utils::urlencode(user.Email)));
                msg->content("text/html");
                outbox->send(std::move(msg));
            }
            else {
                // failed to send email message
                ierror("Attempt to send email to user while mailbox is null");
                Base::fail(resp, "UserRegisterFailure",
                           "Sending registration email failed, try again later or contact system admin");
                resp.end(http::Status::INTERNAL_ERROR);
                return;
            }
#ifndef SWEPT
            resp << "Welcome " << user.FirstName << " " << user.LastName << ", your account was successfully registered."
                 << " A confirmation email has been sent to " << user.Email;
#else
            // When build for swept, we need to return the verification token
            resp << user.Notes;
#endif
            resp.setContentType("text/plain");
            resp.end(http::Status::CREATED);
        }
        catch(...) {
            /* unhandled error */
            ierror("/users/register %s", Exception::fromCurrent().what());
            Base::fail(resp, "InternalError",
                       "Processing register request failed, contact system administrator");
            resp.end(http::Status::INTERNAL_ERROR);
        }
    }

    void Users::loginUser(const suil::http::Request &req, suil::http::Response &resp)
    {
        http::RequestForm requestForm(req, {"Email", "Passwd"}, "\n");
        typedef decltype(iod::D(
                prop(Email, String),
                prop(Passwd, String))
        ) LoginData;

        resp.setContentType("application/json");
        try {
            LoginData data;
            User      user;
            auto why = requestForm >> data;
            if (why) {
                /* missing required fields */
                Base::fail(resp, "MissingFields", std::move(why));
                resp.end(http::Status::FORBIDDEN);
                return;
            }

            scoped(conn,  api.middleware<sql::mw::Postgres>().conn());
            if (!(conn("SELECT * FROM users WHERE email = $1")(data.Email) >> user)) {
                /* failed to read user from database */
                Base::fail(resp, "UserNotRegistered",
                                 "User with email '", data.Email, "' not registered");
                resp.end(http::Status::FORBIDDEN);
                return;
            }

            if (user.State == State::Blocked) {
                /* user blocked */
                Base::fail(resp, "UserBlocked",
                                 "User with email '", data.Email, "' is blocked - ", user.Notes);
                resp.end(http::Status::FORBIDDEN);
                return;
            }

            if (user.State == State::Verify) {
                /* User account needs verification */
                Base::fail(resp, "UserNotVerified",
                                 "User account associated with '", data.Email, "' not verified");
                resp.end(http::Status::FORBIDDEN);
                return;
            }

            if (user.PasswdExpires < time(NULL)) {
                /* Password, has expired, redirect to renew password page */
                Base::fail(resp, "UserPasswordExpired",
                                 "Password associated with '", data.Email, "' is expired, renew password");
                resp.end(http::Status::FORBIDDEN);
                return;
            }

            auto passwd2 = http::pbkdf2_sha1_hash(Gateway::get().PasswdKey())(data.Passwd, user.Salt);
            if (user.Passwd != passwd2) {
                /* invalid password provided */
                Base::fail(resp, "InvalidPassword", "Invalid username/password");
                resp.end(http::Status::FORBIDDEN);
                return;
            }

            /* Login successful, generate token */
            auto& acl = api.context<http::mw::JwtSession>(req);
            if (!acl.authorize(user.Email)) {
                /* no token, create new token */
                http::Jwt token;
                token.aud(user.Email());
                token.claims("id", 8999);
                token.roles(user.Roles);
                acl.authorize(std::move(token));
            }
            resp.setContentType("text/plain");
            resp.end();
        }
        catch (...) {
            /* unhandled error */
            ierror("/login %s", Exception::fromCurrent().what());
            Base::fail(resp, "InternalError",
                             "Processing login request failed, contact system administrator");
            resp.end(http::Status::INTERNAL_ERROR);
        }
    }

    void Users::verifyUser(const suil::http::Request &req, suil::http::Response &resp)
    {
        try {
            /* lookup user and token in database */
            auto email = req.query<String>("email");
            auto token = req.query<String>("id");

            if (!http::validators::Email()(email) || token.empty()) {
                /* invalid user email address */
                Base::fail(resp, "InvalidRequest", "Invalid account verification request");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            scoped(conn, api.template middleware<sql::mw::Postgres>().conn());
            int found{0};
            conn("SELECT COUNT(*) FROM users WHERE email = $1 and notes = $2")(email, token) >> found;
            if (!found) {
                /* does not exist */
                Base::fail(resp, "InvalidRequest", "Account being verified does not exist or has invalid token");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            /* account verified, update */
            bool status = conn("UPDATE users SET State = $1, Notes = $2 WHERE email = $3")((int)State::Active, "", email)
                    .status();
            if (!status) {
                /* updating server failed */
                Base::fail(resp, "InternalError",
                                 "Processing verify request failed, contact system administrator");
                resp.end(http::Status::INTERNAL_ERROR);
                return;
            }

            resp << "Account successfully verified, enjoy :-)";
            resp.setContentType("text/plain");
            resp.end();
        } catch(...) {
            /* unhandled error */
            ierror("/users/verify %s", Exception::fromCurrent().what());
            Base::fail(resp, "InternalError",
                             "Processing account verification request failed, contact system administrator");
            resp.end(http::Status::INTERNAL_ERROR);
        }
    }

    void Users::logoutUser(const suil::http::Request &req, suil::http::Response &resp)
    {
        try {
            /* lookup user and token in database */
            auto email = req.query<String>("email");
            if (!http::validators::Email()(email)) {
                /* invalid user email address */
                Base::fail(resp, "InvalidRequest", "Invalid account logout request");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            /* Nothing complicated, here, just revoke token */
            auto& acl = api.template context<http::mw::JwtSession>(req);
            acl.revoke(email);

            resp << "Successfully logged out";
            resp.setContentType("text/plain");
            resp.end();
        } catch(...) {
            /* unhandled error */
            ierror("/users/logout %s", Exception::fromCurrent().what());
            Base::fail(resp, "InternalError", "Processing logout request failed, contact system administrator");
            resp.end(http::Status::INTERNAL_ERROR);
        }
    }

    void Users::blockUser(const http::Request &req, http::Response &resp)
    {
        try {
            auto email = req.query<String>("email");
            auto reason = req.query<String>("reason");

            if (!http::validators::Email()(email)) {
                /* invalid user email address */
                Base::fail(resp, "InvalidParameters", "Provided account email format is invalid");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            // Revoke all tokens associated with the account to block
            auto& acl = api.template context<http::mw::JwtSession>(req);
            acl.revoke(email);

            // set account status to blocked
            scoped(conn, api.template middleware<sql::mw::Postgres>().conn());
            int found{0};
            conn("SELECT COUNT(*) FROM users WHERE email = $1")(email) >> found;
            if (!found) {
                /* does not exist */
                Base::fail(resp, "InvalidRequest", "Account being verified does not exist");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            /* update account, set it's status to blocked */
            bool status = conn("UPDATE users SET State = $1, Notes = $2 WHERE email = $3")
                              ((int)State::Blocked, reason, email).status();
            if (!status) {
                /* updating server failed */
                Base::fail(resp, "InternalError",
                           "Processing account block status failed");
                resp.end(http::Status::INTERNAL_ERROR);
                return;
            }
        }
        catch (...) {
            /* unhandled error */
            ierror("/users/block %s", Exception::fromCurrent().what());
            Base::fail(resp, "InternalError", "Processing logout request failed, contact system administrator");
            resp.end(http::Status::INTERNAL_ERROR);
        }
    }

    void Users::changePasswd(const http::Request &req, http::Response &resp)
    {
        typedef decltype(iod::D(
            prop(Email,     String),
            prop(OldPasswd, String),
            prop(Passwd,    String)
         )) ChangePasswd;

        http::RequestForm requestForm(req, {"Email", "OldPasswd", "Passwd"}, "\n");

        try {
        }
        catch(...) {
        }
    }
}