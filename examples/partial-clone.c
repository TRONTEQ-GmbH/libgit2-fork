#include "common.h"

#include <ctype.h>

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

	if ((config->username && !config->password) ||
	    (!config->username && config->password)) {
		fprintf(stderr, "username and password must be specified together\n");
		return -1;
	}

	return 0;
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
	int error;

	UNUSED(repo);

	if (argc != 1) {
		fprintf(stderr, "usage: %s partial-clone\n", argv[0]);
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
	options.sparse_checkout = 1;
	options.sparse_checkout_directories = directories;

	printf("Cloning %s (branch %s) into %s\n",
		config.repository_url, config.branch, CLONE_PATH);
	printf("Partial clone filter: blob:none\n");
	printf("Sparse checkout directory: %s\n", config.directory);

	error = git_clone(
		&cloned_repository, config.repository_url, CLONE_PATH, &options);
	if (error < 0) {
		last_error = git_error_last();
		if (last_error)
			fprintf(stderr, "Partial sparse clone failed: %s\n",
				last_error->message);
	}

done:
	git_repository_free(cloned_repository);
	config_dispose(&config);
	return error;
}
