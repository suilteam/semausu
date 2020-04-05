//
// Created by Carter Mbotho on 2020-03-25.
//
#include <suil/mustache.h>
#include <suil/http/validators.h>

#include "users.h"
#include "gateway.h"

namespace suil::nozama {

    constexpr std::int64_t DEFAULT_PASSWORD_EXPIRY{7776000};
    constexpr std::uint32_t DEFAULT_PASSWORD_HISTORY_COUNT{6};

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

        auto& config = Gateway::get().Config();
        Ego.mPasswdExpires = config("accounts.passwordExpires") || DEFAULT_PASSWORD_EXPIRY;
        Ego.mPasswdHistoryCount = config("accounts.passwordHistoryCount") || DEFAULT_PASSWORD_HISTORY_COUNT;
        Ego.mPasswdKey = (String) config("accounts.passwordKey");
        if (Ego.mPasswdKey.empty()) {
            // password key required
            throw Exception::create("Configuration is missing a required 'accounts.passwordKey' value");
        }
    }

    void Users::registerUser_(const http::Request &req, http::Response &resp)
    {
        try {
            http::RequestForm requestForm(req, {"FirstName", "LastName", "Email", "Passwd"}, "; ");
            User      user;
            auto why = requestForm >> user;
            if (why) {
                // missing parameters
                Base::fail(resp, "MissingFields", why);
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            registerUser(req, resp, user);
        }
        catch(...) {
            // Unhandled error
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

        scoped(conn, api.middleware<sql::mw::Postgres>().conn());
        sql::PgSqlTransaction txn(conn);
        defer(_discard, {
            // Defered call to rollback changes that have't been commited
            txn.rollback();
        });

        try {
            if (!EmailValidator(user.Email)) {
                // Invalid user email address
                Base::fail(resp, "InvalidEmailAddress", "email address '", user.Email, "' is invalid");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            OBuffer tmp;
            if (!PasswdValidator(tmp, user.Passwd)) {
                // Invalid user email address
                Base::fail(resp, "InvalidPassword", String{tmp});
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            {
                // limited scope connection
                scoped(conn2, api.middleware<sql::mw::Postgres>().conn());
                int found{0};
                conn("SELECT COUNT(*) FROM users WHERE email like $1")(user.Email) >> found;
                if (found) {
                    // User already registered
                    Base::fail(resp, "UserAlreadyRegistered", "User with email '", user.Email, "' already registered");
                    resp.end(http::Status::BAD_REQUEST);
                    return;
                }
            }

            // Initialize user entities, verification guid
            user.Notes         = utils::uuidstr();
            user.State         = State::Verify;
            user.Salt          = http::rand_8byte_salt()(user.Email);
            user.Passwd        = http::pbkdf2_sha1_hash(mPasswdKey())(user.Passwd, user.Salt);
            user.PasswdExpires = time(nullptr) + mPasswdExpires;

            Table users(conn);
            if (!users.insert(user)) {
                // adding user to database failed
                ierror("failed to add user '%s' to database", user.Email);
                Base::fail(resp, "UserRegisterFailure", "Registering user '",
                           user.Email, "' failed, contact system admin");
                resp.end(http::Status::INTERNAL_ERROR);
                return;
            }

            json::Object params(json::Obj,
                                "name",     user.FirstName.peek(),
                                "endpoint", Gateway::get().Url.peek(),
                                "token",    utils::urlencode(user.Notes),
                                "email",    utils::urlencode(user.Email));
            auto sent = Gateway::get().sendTemplatedEmail(user.Email, "Account successfully Registered",
                                              "_verify_account.html", params);
            if (!sent) {
                // Failed to send email message
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

            txn.commit();
        }
        catch(...) {
            // unhandled error
            ierror("/users/register %s", Exception::fromCurrent().what());
            Base::fail(resp, "InternalError",
                       "Processing register request failed, contact system administrator");
            resp.end(http::Status::INTERNAL_ERROR);
        }
    }

    void Users::loginUser(const suil::http::Request &req, suil::http::Response &resp)
    {
        http::RequestForm requestForm(req, {"Email", "Passwd"}, ";");
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
                // Missing required fields
                Base::fail(resp, "MissingFields", std::move(why));
                resp.end(http::Status::FORBIDDEN);
                return;
            }

            {
                scoped(conn, api.middleware<sql::mw::Postgres>().conn());
                if (!(conn("SELECT * FROM users WHERE email = $1")(data.Email) >> user)) {
                    // User does not exist
                    Base::fail(resp, "UserNotRegistered",
                               "User with email '", data.Email, "' not registered");
                    resp.end(http::Status::FORBIDDEN);
                    return;
                }
            }

            if (user.State == State::Blocked) {
                // User blocked
                Base::fail(resp, "UserBlocked",
                                 "User with email '", data.Email, "' is blocked - ", user.Notes);
                resp.end(http::Status::FORBIDDEN);
                return;
            }

            if (user.State == State::Verify) {
                // User account needs verification
                Base::fail(resp, "UserNotVerified",
                                 "User account associated with '", data.Email, "' not verified");
                resp.end(http::Status::FORBIDDEN);
                return;
            }

            if (user.PasswdExpires < time(NULL)) {
                // Password, has expired, redirect to renew password page
                Base::fail(resp, "UserPasswordExpired",
                                 "Password associated with '", data.Email, "' is expired, renew password");
                resp.end(http::Status::FORBIDDEN);
                return;
            }

            auto passwd2 = http::pbkdf2_sha1_hash(mPasswdKey())(data.Passwd, user.Salt);
            if (user.Passwd != passwd2) {
                // invalid password provided
                Base::fail(resp, "InvalidPassword", "Invalid username/password");
                resp.end(http::Status::FORBIDDEN);
                return;
            }

            // Login successful, generate token
            auto& acl = api.context<http::mw::JwtSession>(req);
            if (!acl.authorize(user.Email)) {
                // no token, create new token
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
            // unhandled error
            ierror("/users/login %s", Exception::fromCurrent().what());
            Base::fail(resp, "InternalError",
                             "Processing login request failed, contact system administrator");
            resp.end(http::Status::INTERNAL_ERROR);
        }
    }

    void Users::verifyUser(const suil::http::Request &req, suil::http::Response &resp)
    {
        try {
            // Lookup user and token in database
            auto email = req.query<String>("email");
            auto token = req.query<String>("id");

            if (!http::validators::Email()(email) || token.empty()) {
                // Invalid user email address
                Base::fail(resp, "InvalidRequest", "Invalid account verification request");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            scoped(conn, api.template middleware<sql::mw::Postgres>().conn());
            int found{0};
            conn("SELECT COUNT(*) FROM users WHERE email = $1 and notes = $2")(email, token) >> found;
            if (!found) {
                // Does not exist
                Base::fail(resp, "InvalidRequest", "Account being verified does not exist or has invalid token");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            // Account verified, update
            bool status = conn("UPDATE users SET State = $1, Notes = $2 WHERE email = $3")((int)State::Active, "", email)
                    .status();
            if (!status) {
                // Updating server failed
                Base::fail(resp, "InternalError",
                                 "Processing verify request failed, contact system administrator");
                resp.end(http::Status::INTERNAL_ERROR);
                return;
            }

            resp << "Account successfully verified, enjoy :-)";
            resp.setContentType("text/plain");
            resp.end();
        } catch(...) {
            // Unhandled error
            ierror("/users/verify %s", Exception::fromCurrent().what());
            Base::fail(resp, "InternalError",
                             "Processing account verification request failed, contact system administrator");
            resp.end(http::Status::INTERNAL_ERROR);
        }
    }

    void Users::logoutUser(const suil::http::Request &req, suil::http::Response &resp)
    {
        try {
            // lookup user and token in database
            auto email = req.query<String>("email");
            if (!http::validators::Email()(email)) {
                // invalid user email address
                Base::fail(resp, "InvalidRequest", "Invalid account logout request");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            // Nothing complicated, here, just revoke token
            auto& acl = api.template context<http::mw::JwtSession>(req);
            acl.revoke(email);

            resp << "Successfully logged out";
            resp.setContentType("text/plain");
            resp.end();
        } catch(...) {
            // Unhandled error
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

            // Revoke all tokens associated with the account to block
            auto& acl = api.template context<http::mw::JwtSession>(req);
            acl.revoke(email);

            // set account status to blocked
            scoped(conn, api.template middleware<sql::mw::Postgres>().conn());
            int found{0};
            conn("SELECT COUNT(*) FROM users WHERE email = $1")(email) >> found;
            if (!found) {
                // account does not exist
                Base::fail(resp, "InvalidRequest", "Account being verified does not exist");
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            // update account, set it's status to blocked
            bool status = conn("UPDATE users SET State = $1, Notes = $2 WHERE email = $3")
                              ((int)State::Blocked, reason, email).status();
            if (!status) {
                // updating server failed
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

        http::validators::Password PasswdValidator;
        http::RequestForm requestForm(req, {"Email", "OldPasswd", "Passwd"}, "; ");

        scoped(conn, api.middleware<sql::mw::Postgres>().conn());
        sql::PgSqlTransaction txn(conn);
        defer(_discard, {
            // Defered call to rollback changes that have't been commited
            txn.rollback();
        });

        try {
            ChangePasswd  changes{"", "", ""};
            auto why = requestForm >> changes;
            if (why) {
                // missing required fields
                Base::fail(resp, "MissingFields", std::move(why));
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            OBuffer tmp;
            if (!PasswdValidator(tmp, changes.Passwd)) {
                // invalid password format
                Base::fail(resp, "InvalidPassword", String{tmp});
                resp.end(http::Status::BAD_REQUEST);
                return;
            }

            User user;
            {
                scoped(conn2, api.middleware<sql::mw::Postgres>().conn());
                if (!(conn2("SELECT * FROM users WHERE Email = '$1'")(changes.Email) >> user)) {
                    Base::fail(resp, "UserNoRegistered", "User with email '%s' is not registered", changes.Email);
                    resp.end(http::BAD_REQUEST);
                    return;
                }
            }

            // old password must match current password
            auto passwdHashed = http::pbkdf2_sha1_hash(mPasswdKey())(changes.OldPasswd, user.Salt);
            if (user.Passwd != passwdHashed) {
                /* invalid password provided */
                Base::fail(resp, "InvalidPassword", "Invalid old password");
                resp.end(http::BAD_REQUEST);
                return;
            }
            // new password must not be the same as last 6 passwords
            auto newPasswdHashed = http::pbkdf2_sha1_hash(mPasswdKey())(changes.Passwd, user.Salt);
            for (const auto& passwd: user.PrevPasswds) {
                if (newPasswdHashed == passwd) {
                    Base::fail(resp, "InvalidPassword", "Invalid new password, password was recently used");
                    resp.end(http::BAD_REQUEST);
                    return;
                }
            }
            // Add current password to a list of old password
            if (user.PrevPasswds.size() >= mPasswdHistoryCount) {
                // Limit password history
                user.PrevPasswds.erase(user.PrevPasswds.begin());
            }
            user.PrevPasswds.push_back(newPasswdHashed.peek());
            user.Passwd        = newPasswdHashed.peek();
            user.PasswdExpires = time(nullptr) + mPasswdExpires;
            // Revoke any tokens associated with account
            auto& acl = api.template context<http::mw::JwtSession>(req);
            acl.revoke(changes.Email);

            // Push changes to database
            Table orm(conn);
            if (!orm.update(user)) {
                ierror("Updating user '%s' password changed failed");
                Base::fail(resp, "SystemError", "Change account password fail, contact system administrator");
                resp.end(http::INTERNAL_ERROR);
                return;
            }

            // Notify user of password change
            json::Object params(json::Obj,
                                "frontend", Gateway::get().Frontend);
            auto sent = Gateway::get().sendTemplatedEmail(
                    changes.Email, "Semausu: Password change successful", "_password_changed.html", params);
            if (!sent) {
                // Failed to send email message
                ierror("Sending password changed email to user failed");
                Base::fail(resp, "SystemError",
                           "Sending password changed email failed, try again later or contact system admin");
                resp.end(http::Status::INTERNAL_ERROR);
                return;
            }
            // Commit database transaction
            resp << "Password successfully changed";

            txn.commit();
        }
        catch(...) {
            /* unhandled error */
            ierror("/users/changepasswd %s", Exception::fromCurrent().what());
            Base::fail(resp, "InternalError", "Processing change password request failed, contact system administrator");
            resp.end(http::Status::INTERNAL_ERROR);
        }
    }
}