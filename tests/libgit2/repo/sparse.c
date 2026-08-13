#include "clar_libgit2.h"
#include "repository.h"
#include "futils.h"

static git_repository *g_repo;

void test_repo_sparse__initialize(void)
{
	g_repo = cl_git_sandbox_init("testrepo");
}

void test_repo_sparse__cleanup(void)
{
	cl_git_sandbox_cleanup();
}

void test_repo_sparse__configures_cone_mode(void)
{
	char *directories[] = {
		"src/libgit2",
		"tests",
	};
	git_strarray sparse_directories = {
		directories,
		ARRAY_SIZE(directories),
	};
	git_config *config;
	git_str path = GIT_STR_INIT;
	git_str patterns = GIT_STR_INIT;
	int value;

	cl_git_pass(git_sparse_checkout_set(g_repo, &sparse_directories));

	cl_git_pass(git_repository_config(&config, g_repo));
	cl_git_pass(git_config_get_bool(&value, config, "core.sparsecheckout"));
	cl_assert(value);
	cl_git_pass(
	        git_config_get_bool(&value, config, "core.sparsecheckoutcone"));
	cl_assert(value);

	cl_git_pass(git_repository__item_path(
	        &path, g_repo, GIT_REPOSITORY_ITEM_INFO));
	cl_git_pass(git_str_joinpath(&path, path.ptr, "sparse-checkout"));
	cl_git_pass(git_futils_readbuffer(&patterns, path.ptr));

	cl_assert_equal_s(
	        "/*\n"
	        "!/*/\n"
	        "/src/\n"
	        "!/src/*/\n"
	        "/src/libgit2/\n"
	        "/tests/\n",
	        patterns.ptr);

	git_str_dispose(&patterns);
	git_str_dispose(&path);
	git_config_free(config);
}
