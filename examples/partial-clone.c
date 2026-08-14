#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <git2/sys/errors.h>

#define CONFIG_FILE "git-lg2.conf"
#define CLONE_PATH "test-lg2-clone"
#define CONFIG_LINE_MAX 4096

typedef struct {
	char *repository_url;
	char *branch;
	char *directory;
	char *username;
	char *password;
} partial_clone_config;

typedef struct {
	const char *base_path;
	int error;
} directory_walk_data;

static void config_dispose(partial_clone_config *config)
{
	free(config->repository_url);
	free(config->branch);
	free(config->directory);
	free(config->username);

	if (config->password) {
		memset(config->password, 0, strlen(config->password));
		free(config->password);
	}

	memset(config, 0, sizeof(*config));
}

static char *string_dup(const char *value)
{
	char *result;
	size_t length;

	length = strlen(value) + 1;
	result = malloc(length);
	if (!result)
		return NULL;

	memcpy(result, value, length);
	return result;
}

static char *trim(char *value)
{
	char *end;

	while (*value && isspace((unsigned char)*value))
		value++;

	end = value + strlen(value);
	while (end > value && isspace((unsigned char)end[-1]))
		*--end = '\0';

	return value;
}

static int config_set(char **out, const char *value)
{
	char *copy;

	copy = string_dup(value);
	if (!copy)
		return -1;

	free(*out);
	*out = copy;
	return 0;
}

static int directory_path_is_safe(const char *path)
{
	const char *component;
	const char *separator;
	size_t length;

	if (!*path || path[0] == '/' || strchr(path, '\\'))
		return 0;

	component = path;
	while (1) {
		separator = strchr(component, '/');
		length = separator ? (size_t)(separator - component) :
			strlen(component);

		if (!length ||
		    (length == 1 && component[0] == '.') ||
		    (length == 2 && component[0] == '.' &&
		     component[1] == '.'))
			return 0;

		if (!separator)
			return 1;

		component = separator + 1;
	}
}

static int config_read(partial_clone_config *config)
{
	FILE *file;
	char line[CONFIG_LINE_MAX];
	unsigned int line_number = 0;
	int error = 0;

	file = fopen(CONFIG_FILE, "r");
	if (!file) {
		fprintf(stderr, "Cannot open %s\n", CONFIG_FILE);
		return -1;
	}

	while (fgets(line, sizeof(line), file)) {
		char *equals, *key, *value;

		line_number++;
		key = trim(line);

		if (!*key || *key == '#')
			continue;

		equals = strchr(key, '=');
		if (!equals) {
			fprintf(stderr, "%s:%u: expected key=value\n",
				CONFIG_FILE, line_number);
			error = -1;
			break;
		}

		*equals = '\0';
		key = trim(key);
		value = trim(equals + 1);

		if (!strcmp(key, "repository_url"))
			error = config_set(&config->repository_url, value);
		else if (!strcmp(key, "branch"))
			error = config_set(&config->branch, value);
		else if (!strcmp(key, "directory"))
			error = config_set(&config->directory, value);
		else if (!strcmp(key, "username"))
			error = config_set(&config->username, value);
		else if (!strcmp(key, "password"))
			error = config_set(&config->password, value);
		else {
			fprintf(stderr, "%s:%u: unknown key '%s'\n",
				CONFIG_FILE, line_number, key);
			error = -1;
		}

		if (error < 0)
			break;
	}

	fclose(file);

	if (error < 0) {
		fprintf(stderr, "%s: invalid configuration or out of memory\n",
			CONFIG_FILE);
		return error;
	}

	if (!config->repository_url || !*config->repository_url ||
	    !config->branch || !*config->branch ||
	    !config->directory || !*config->directory) {
		fprintf(stderr,
			"%s must define repository_url, branch and directory\n",
			CONFIG_FILE);
		return -1;
	}

	if (!directory_path_is_safe(config->directory)) {
		fprintf(stderr, "%s: directory must be a relative repository path\n",
			CONFIG_FILE);
		return -1;
	}

	if ((config->username && !config->password) ||
	    (!config->username && config->password)) {
		fprintf(stderr, "username and password must be specified together\n");
		return -1;
	}

	return 0;
}

static int path_join(char **out, const char *base, const char *path)
{
	size_t base_length = strlen(base);
	size_t path_length = strlen(path);
	size_t separator = base_length && path_length &&
		base[base_length - 1] != '/';
	char *result;

	if ((result = malloc(base_length + separator + path_length + 1)) == NULL)
		return -1;

	memcpy(result, base, base_length);
	if (separator)
		result[base_length++] = '/';

	memcpy(result + base_length, path, path_length + 1);
	*out = result;
	return 0;
}

static int create_directory(const char *path)
{
	struct stat st;

#ifdef _WIN32
	if (_mkdir(path) < 0 && errno != EEXIST)
#else
	if (mkdir(path, 0777) < 0 && errno != EEXIST)
#endif
	{
		git_error_set(GIT_ERROR_OS,
			"could not create directory '%s': %s", path, strerror(errno));
		return -1;
	}

	if (stat(path, &st) < 0 || !S_ISDIR(st.st_mode)) {
		git_error_set(GIT_ERROR_OS,
			"path '%s' is not a directory", path);
		return -1;
	}

	return 0;
}

static int create_tree_directory(
	const char *root,
	const git_tree_entry *entry,
	void *payload)
{
	directory_walk_data *data = payload;
	char *relative_path = NULL;
	char *directory_path = NULL;
	int error;

	if (git_tree_entry_type(entry) != GIT_OBJECT_TREE)
		return 0;

	if ((error = path_join(&relative_path, root, git_tree_entry_name(entry))) < 0 ||
	    (error = path_join(&directory_path, data->base_path, relative_path)) < 0 ||
	    (error = create_directory(directory_path)) < 0)
		data->error = error;

	free(relative_path);
	free(directory_path);
	return error;
}

static int create_directory_tree(
	git_repository *repo,
	const partial_clone_config *config)
{
	git_object *object = NULL;
	git_tree *head_tree;
	git_tree *selected_tree = NULL;
	git_tree_entry *entry = NULL;
	directory_walk_data data;
	char *base_path = NULL;
	int error;

	if ((error = git_revparse_single(&object, repo, "HEAD^{tree}")) < 0)
		goto done;

	head_tree = (git_tree *)object;
	if ((error = git_tree_entry_bypath(
		     &entry, head_tree, config->directory)) < 0)
		goto done;

	if (git_tree_entry_type(entry) != GIT_OBJECT_TREE) {
		git_error_set(GIT_ERROR_INVALID,
			"'%s' is not a directory", config->directory);
		error = -1;
		goto done;
	}

	if ((error = git_tree_lookup(
		     &selected_tree, repo, git_tree_entry_id(entry))) < 0 ||
	    (error = path_join(&base_path, CLONE_PATH, config->directory)) < 0 ||
	    (error = create_directory(base_path)) < 0)
		goto done;

	data.base_path = base_path;
	data.error = 0;
	if ((error = git_tree_walk(
		     selected_tree, GIT_TREEWALK_PRE, create_tree_directory, &data)) < 0)
		goto done;

	error = data.error;

done:
	free(base_path);
	git_tree_entry_free(entry);
	git_tree_free(selected_tree);
	git_object_free(object);
	return error;
}

static int credentials_cb(
	git_credential **out,
	const char *url,
	const char *username_from_url,
	unsigned int allowed_types,
	void *payload)
{
	partial_clone_config *config = payload;

	UNUSED(url);
	UNUSED(username_from_url);

	if (!config->username || !*config->username)
		return GIT_PASSTHROUGH;

	if (allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT)
		return git_credential_userpass_plaintext_new(
			out, config->username, config->password);

	if (allowed_types & GIT_CREDENTIAL_USERNAME)
		return git_credential_username_new(out, config->username);

	return GIT_PASSTHROUGH;
}

static int transfer_progress(
	const git_indexer_progress *stats,
	void *payload)
{
	UNUSED(payload);

	if (stats->total_objects &&
	    stats->received_objects == stats->total_objects)
		printf("Received %u objects\n", stats->received_objects);

	return 0;
}

int lg2_partial_clone(git_repository *repo, int argc, char **argv)
{
	partial_clone_config config = { 0 };
	git_clone_options options = GIT_CLONE_OPTIONS_INIT;
	git_repository *cloned_repository = NULL;
	git_strarray directories;
	char *directory_strings[1];
	const git_error *last_error;
	int directories_only;
	int error;

	UNUSED(repo);

	directories_only = argc == 2 &&
		!strcmp(argv[1], "--directories-only");

	if (argc != 1 && !directories_only) {
		fprintf(stderr,
			"usage: %s partial-clone [--directories-only]\n", argv[0]);
		return -1;
	}

	if ((error = config_read(&config)) < 0)
		goto done;

	directory_strings[0] = config.directory;
	directories.strings = directory_strings;
	directories.count = 1;

	options.checkout_branch = config.branch;
	options.fetch_opts.filter_spec = "blob:none";
	options.fetch_opts.callbacks.credentials = credentials_cb;
	options.fetch_opts.callbacks.transfer_progress = transfer_progress;
	options.fetch_opts.callbacks.payload = &config;

	printf("Cloning %s (branch %s) into %s\n",
		config.repository_url, config.branch, CLONE_PATH);
	printf("Partial clone filter: blob:none\n");

	if (directories_only) {
		options.checkout_opts.checkout_strategy = GIT_CHECKOUT_NONE;
		printf("Creating directory tree only: %s\n", config.directory);
	} else {
		options.sparse_checkout = 1;
		options.sparse_checkout_directories = directories;
		printf("Sparse checkout directory: %s\n", config.directory);
	}

	error = git_clone(
		&cloned_repository, config.repository_url, CLONE_PATH, &options);
	if (!error && directories_only)
		error = create_directory_tree(cloned_repository, &config);

	if (error < 0) {
		last_error = git_error_last();
		if (last_error)
			fprintf(stderr, "Partial clone failed: %s\n",
				last_error->message);
	}

done:
	git_repository_free(cloned_repository);
	config_dispose(&config);
	return error;
}
