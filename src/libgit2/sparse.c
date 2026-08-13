/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include "git2/sparse.h"
#include "git2/index.h"

#include "repository.h"
#include "config.h"
#include "filebuf.h"
#include "futils.h"
#include "path.h"

static int validate_directory(git_repository *repo, const char *directory)
{
	const char *component, *slash;

	if (!directory || !*directory || directory[0] == '/' ||
	    directory[strlen(directory) - 1] == '/' ||
	    strchr(directory, '\\') || strstr(directory, "//")) {
		git_error_set(
		        GIT_ERROR_INVALID,
		        "sparse checkout directory must be a relative path");
		return GIT_EINVALID;
	}

	for (component = directory; component; component = slash + 1) {
		slash = strchr(component, '/');

		if ((slash && slash == component) ||
		    (!slash && component[0] == '\0') ||
		    ((slash ? (size_t)(slash - component) :
		              strlen(component)) == 1 &&
		     component[0] == '.') ||
		    ((slash ? (size_t)(slash - component) :
		              strlen(component)) == 2 &&
		     component[0] == '.' && component[1] == '.')) {
			git_error_set(
			        GIT_ERROR_INVALID,
			        "sparse checkout directory contains an invalid component");
			return GIT_EINVALID;
		}

		if (!slash)
			break;
	}

	if (!git_path_is_valid(
	            repo, directory, GIT_FILEMODE_TREE,
	            GIT_PATH_REJECT_WORKDIR_DEFAULTS)) {
		git_error_set(
		        GIT_ERROR_INVALID,
		        "invalid sparse checkout directory '%s'", directory);
		return GIT_EINVALID;
	}

	return 0;
}

static int
write_cone_patterns(git_filebuf *file, const git_strarray *directories)
{
	size_t i;
	int error;

	if ((error = git_filebuf_write(file, "/*\n!/*/\n", 8)) < 0)
		return error;

	for (i = 0; i < directories->count; i++) {
		const char *directory = directories->strings[i];
		const char *slash;
		size_t length;

		for (slash = directory;; slash++) {
			if (*slash != '/' && *slash != '\0')
				continue;

			length = (size_t)(slash - directory);

			if ((error = git_filebuf_printf(
			             file, "/%.*s/\n", (int)length,
			             directory)) < 0)
				return error;

			if (*slash == '\0')
				break;

			if ((error = git_filebuf_printf(
			             file, "!/%.*s/*/\n", (int)length,
			             directory)) < 0)
				return error;
		}
	}

	return 0;
}

static int sparse_checkout_path(git_str *path, git_repository *repo)
{
	if (git_repository__item_path(path, repo, GIT_REPOSITORY_ITEM_INFO) < 0)
		return -1;

	return git_str_joinpath(path, path->ptr, "sparse-checkout");
}

static bool sparse_checkout_pattern_is_parent(
        const char *pattern,
        size_t pattern_len,
        const char *next,
        const char *end)
{
	const char *next_end;
	size_t next_len;

	if (!next || next >= end)
		return false;

	next_end = memchr(next, '\n', (size_t)(end - next));
	if (!next_end)
		next_end = end;

	next_len = (size_t)(next_end - next);

	return next_len == pattern_len + 3 && next[0] == '!' &&
	       memcmp(next + 1, pattern, pattern_len - 1) == 0 &&
	       memcmp(next + pattern_len, "/*/", 3) == 0;
}

static bool
sparse_checkout_path_is_in_cone(const char *path, const git_str *patterns)
{
	const char *line = patterns->ptr;
	const char *end = patterns->ptr + patterns->size;

	if (!strchr(path, '/'))
		return true;

	while (line < end) {
		const char *line_end = memchr(line, '\n', (size_t)(end - line));
		const char *next;
		size_t line_len, directory_len;
		bool is_parent;

		if (!line_end)
			line_end = end;

		line_len = (size_t)(line_end - line);
		next = line_end == end ? NULL : line_end + 1;

		if (line_len < 3 || line[0] != '/' ||
		    line[line_len - 1] != '/' || line[1] == '*')
			goto next;

		directory_len = line_len - 2;

		if (strncmp(path, line + 1, directory_len) != 0 ||
		    path[directory_len] != '/')
			goto next;

		is_parent = sparse_checkout_pattern_is_parent(
		        line, line_len, next, end);

		if (!is_parent || !strchr(path + directory_len + 1, '/'))
			return true;

	next:
		line = line_end == end ? end : line_end + 1;
	}

	return false;
}

int git_sparse_checkout_set(
        git_repository *repo,
        const git_strarray *directories)
{
	git_config *config;
	git_filebuf file = GIT_FILEBUF_INIT;
	git_str path = GIT_STR_INIT;
	size_t i;
	int error = 0;
	bool file_open = false;

	GIT_ASSERT_ARG(repo);
	GIT_ASSERT_ARG(directories);

	if (git_repository_is_bare(repo)) {
		git_error_set(
		        GIT_ERROR_REPOSITORY,
		        "cannot configure sparse checkout in a bare repository");
		return GIT_EBAREREPO;
	}

	if (directories->count && !directories->strings)
		return GIT_EINVALID;

	for (i = 0; i < directories->count; i++) {
		if ((error = validate_directory(
		             repo, directories->strings[i])) < 0)
			goto done;
	}

	if ((error = sparse_checkout_path(&path, repo)) < 0 ||
	    (error = git_filebuf_open(
	             &file, path.ptr, GIT_FILEBUF_CREATE_LEADING_DIRS, 0666)) <
	            0)
		goto done;

	file_open = true;

	if ((error = write_cone_patterns(&file, directories)) < 0 ||
	    (error = git_filebuf_commit(&file)) < 0)
		goto done;

	file_open = false;

	if ((error = git_repository_config__weakptr(&config, repo)) < 0 ||
	    (error = git_config_set_bool(config, "core.sparsecheckout", true)) <
	            0 ||
	    (error = git_config_set_bool(
	             config, "core.sparsecheckoutcone", true)) < 0)
		goto done;

done:
	if (file_open)
		git_filebuf_cleanup(&file);

	git_str_dispose(&path);
	return error;
}

int git_sparse_checkout_update_index(git_repository *repo)
{
	git_index *index = NULL;
	git_index_iterator *iterator = NULL;
	const git_index_entry *entry;
	git_str path = GIT_STR_INIT;
	git_str patterns = GIT_STR_INIT;
	int error;

	GIT_ASSERT_ARG(repo);

	if (git_repository_is_bare(repo)) {
		git_error_set(
		        GIT_ERROR_REPOSITORY,
		        "cannot update sparse checkout index in a bare repository");
		return GIT_EBAREREPO;
	}

	if ((error = sparse_checkout_path(&path, repo)) < 0 ||
	    (error = git_futils_readbuffer(&patterns, path.ptr)) < 0 ||
	    (error = git_repository_index(&index, repo)) < 0 ||
	    (error = git_index_iterator_new(&iterator, index)) < 0)
		goto done;

	while ((error = git_index_iterator_next(&entry, iterator)) == 0) {
		git_index_entry updated = *entry;
		bool included =
		        sparse_checkout_path_is_in_cone(entry->path, &patterns);

		if (GIT_INDEX_ENTRY_STAGE(entry) != 0)
			continue;

		if (included)
			updated.flags_extended &=
			        ~GIT_INDEX_ENTRY_SKIP_WORKTREE;
		else
			updated.flags_extended |= GIT_INDEX_ENTRY_SKIP_WORKTREE;

		if (updated.flags_extended != entry->flags_extended &&
		    (error = git_index_add(index, &updated)) < 0)
			goto done;
	}

	if (error == GIT_ITEROVER)
		error = git_index_write(index);

done:
	git_index_iterator_free(iterator);
	git_index_free(index);
	git_str_dispose(&patterns);
	git_str_dispose(&path);
	return error;
}
