#include "package.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_valid(const char *name)
{
    char *error = NULL;
    bool valid = package_name_valid(name, &error);
    if (!valid) {
        fprintf(stderr, "expected valid: %s (%s)\n", name,
                error != NULL ? error : "?");
    }
    assert(valid);
    assert(error == NULL);
}

static void expect_invalid(const char *name)
{
    char *error = NULL;
    bool valid = package_name_valid(name, &error);
    assert(!valid);
    assert(error != NULL);
    free(error);
}

int main(void)
{
    expect_valid("hello");
    expect_valid("python3Packages.requests");
    expect_valid("nodePackages.@angular/cli");
    expect_valid("_1password");
    expect_valid("foo+bar");

    expect_invalid("");
    expect_invalid(".");
    expect_invalid(".foo");
    expect_invalid("foo.");
    expect_invalid("foo..bar");
    expect_invalid("foo bar");
    expect_invalid("foo\nbar");

    char long_component[130];
    memset(long_component, 'a', 129);
    long_component[129] = '\0';
    expect_invalid(long_component);

    PackageInfo info;
    package_info_init(&info);
    package_info_free(&info);

    puts("test_package: ok");
    return 0;
}
