//
// Created by Carter Mbotho on 2020-03-24.
//
void main(int argc, char *argv[]) {
    Register reg{"email@example.com", "secret", {"Firstname", "String", "Carter"}};
    Action action{reg};
    send(action);
    auto req = recieve();
    auto reg_ = req.get<Register>();
    using Command = Union<Register,Login>;
}

