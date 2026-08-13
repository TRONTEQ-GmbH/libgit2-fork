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

void test_repo_sparse__updates_skip_worktree_flags(void)
{
	char *directories[] = {
		"src/libgit2",
	};
	git_strarray sparse_directories = {
		directories,
		ARRAY_SIZE(directories),
	};
	git_index *index;
	git_index_entry entry = { 0 };
	const git_index_entry *indexed;

	entry.mode = GIT_FILEMODE_BLOB;
	git_oid_fromstr(&entry.id, "a71586c1dfe8a71c6cbf6c129f404c5642ff31bd");

	cl_git_pass(git_repository_index(&index, g_repo));
	cl_git_pass(git_index_clear(index));

	entry.path = "README";
	cl_git_pass(git_index_add(index, &entry));
	entry.path = "src/README";
	cl_git_pass(git_index_add(index, &entry));
	entry.path = "src/libgit2/repository.c";
	cl_git_pass(git_index_add(index, &entry));
	entry.path = "src/other/file.c";
	cl_git_pass(git_index_add(index, &entry));
	entry.path = "docs/guide.md";
	cl_git_pass(git_index_add(index, &entry));
	entry.path = "conflicted.c";
	GIT_INDEX_ENTRY_STAGE_SET(&entry, GIT_INDEX_STAGE_OURS);
	cl_git_pass(git_index_add(index, &entry));
	cl_git_pass(git_index_write(index));

	cl_git_pass(git_sparse_checkout_set(g_repo, &sparse_directories));
	cl_git_pass(git_sparse_checkout_update_index(g_repo));

	indexed = git_index_get_bypath(index, "README", 0);
	cl_assert(
	        (indexed->flags_extended & GIT_INDEX_ENTRY_SKIP_WORKTREE) == 0);
	indexed = git_index_get_bypath(index, "src/README", 0);
	cl_assert(
	        (indexed->flags_extended & GIT_INDEX_ENTRY_SKIP_WORKTREE) == 0);
	indexed = git_index_get_bypath(index, "src/libgit2/repository.c", 0);
	cl_assert(
	        (indexed->flags_extended & GIT_INDEX_ENTRY_SKIP_WORKTREE) == 0);
	indexed = git_index_get_bypath(index, "src/other/file.c", 0);
	cl_assert(
	        (indexed->flags_extended & GIT_INDEX_ENTRY_SKIP_WORKTREE) != 0);
	indexed = git_index_get_bypath(index, "docs/guide.md", 0);
	cl_assert(
	        (indexed->flags_extended & GIT_INDEX_ENTRY_SKIP_WORKTREE) != 0);
	indexed = git_index_get_bypath(
	        index, "conflicted.c", GIT_INDEX_STAGE_OURS);
	cl_assert(
	        (indexed->flags_extended & GIT_INDEX_ENTRY_SKIP_WORKTREE) == 0);

	git_index_free(index);
}

void test_repo_sparse__initializes_empty_index_from_head(void)
{
	git_strarray sparse_directories = {
		NULL,
		0,
	};
	git_index *index;
	const git_index_entry *entry;

	cl_git_pass(git_repository_index(&index, g_repo));
	cl_git_pass(git_index_clear(index));
	cl_git_pass(git_index_write(index));

	cl_git_pass(git_sparse_checkout_set(g_repo, &sparse_directories));
	cl_git_pass(git_sparse_checkout_initialize_index(g_repo));

	cl_assert_equal_i(4, git_index_entrycount(index));

	entry = git_index_get_bypath(index, "README", 0);
	cl_assert(entry);
	cl_assert((entry->flags_extended & GIT_INDEX_ENTRY_SKIP_WORKTREE) == 0);

	git_index_free(index);
}

void test_repo_sparse__checks_out_included_paths(void)
{
	git_strarray sparse_directories = {
		NULL,
		0,
	};
	git_index *index;

	cl_git_pass(git_repository_index(&index, g_repo));
	cl_git_pass(git_index_clear(index));
	cl_git_pass(git_index_write(index));

	cl_git_pass(git_sparse_checkout_set(g_repo, &sparse_directories));
	cl_git_pass(git_sparse_checkout_initialize_index(g_repo));
	cl_git_pass(p_unlink("testrepo/README"));
	cl_assert(!git_fs_path_isfile("testrepo/README"));

	cl_git_pass(git_sparse_checkout_checkout(g_repo));
	cl_assert(git_fs_path_isfile("testrepo/README"));

	git_index_free(index);
}
