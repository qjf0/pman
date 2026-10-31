struct user {
        size_t uid;
        char name[64];
        char password[64];
        char allowed[64 * 128];
};

static const struct user user_config[] = {
        {0, "su", "password",
                "*"
        },

        {1, "normal_user", "password",
                "article:*\n"
                "user:normal_user:info"
        }
};
