#include "channel.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static void expect_branch(const char *input, const char *expected)
{
    char *branch = channel_branch_from_version(input);
    if (branch == NULL) {
        branch = channel_branch_from_name(input);
    }
    assert(branch != NULL);
    if (strcmp(branch, expected) != 0) {
        fprintf(stderr, "input=%s expected=%s got=%s\n", input, expected,
                branch);
        assert(0);
    }
    free(branch);
}

static void test_versions(void)
{
    expect_branch("26.11", "nixos-26.11");
    expect_branch("26.11.20261020.02c087f", "nixos-26.11");
    expect_branch("26.11pre-git", "nixos-unstable");
    expect_branch("nixos-unstable", "nixos-unstable");
    expect_branch("nixpkgs-25.05-darwin", "nixos-25.05");
    expect_branch("https://nixos.org/channels/nixos-25.05", "nixos-25.05");
    expect_branch("https://nixos.org/channels/nixpkgs-unstable",
                  "nixos-unstable");
}

static void test_explicit_channel(void)
{
    Channel channel;
    char *error = NULL;

    assert(channel_detect(&channel, "25.05", false, &error) == 0);
    assert(strcmp(channel.branch, "nixos-25.05") == 0);
    assert(strcmp(channel.version, "25.05") == 0);
    assert(strcmp(channel.ref, "github:NixOS/nixpkgs/nixos-25.05") == 0);
    assert(error == NULL);
    channel_free(&channel);

    assert(channel_detect(&channel, "unstable", false, &error) == 0);
    assert(strcmp(channel.branch, "nixos-unstable") == 0);
    assert(strcmp(channel.version, "unstable") == 0);
    channel_free(&channel);
}

static void test_invalid_channel(void)
{
    Channel channel;
    char *error = NULL;
    assert(channel_detect(&channel, "wat", false, &error) == -1);
    assert(error != NULL);
    free(error);
}

int main(void)
{
    test_versions();
    test_explicit_channel();
    test_invalid_channel();
    puts("test_channel: ok");
    return 0;
}
