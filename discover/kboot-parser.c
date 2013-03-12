#define _GNU_SOURCE

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "log/log.h"
#include "talloc/talloc.h"
#include "types/types.h"
#include "parser-conf.h"
#include "parser-utils.h"
#include "paths.h"

static void kboot_process_pair(struct conf_context *conf, const char *name,
		char *value)
{
	const char *const *ignored_names = conf->parser_info;
	struct device *dev;
	char *pos;
	char *args;
	const char *initrd;
	const char *root;
	struct boot_option *opt;

	/* ignore bare values */

	if (!name)
		return;

	if (conf_param_in_list(ignored_names, name))
		return;

	if (conf_set_global_option(conf, name, value))
		return;

	/* opt must be associated with dc */

	dev = conf->dc->device->device;
	opt = talloc_zero(dev, struct boot_option);

	if (!opt)
		return;

	opt->id = talloc_asprintf(opt, "%s#%s", dev->id, name);
	opt->name = talloc_strdup(opt, name);

	args = talloc_strdup(opt, "");
	initrd = conf_get_global_option(conf, "initrd");
	root = conf_get_global_option(conf, "root");

	pos = strchr(value, ' ');

	/* if there's no space, it's only a kernel image with no params */

	if (!pos)
		goto out_add;
	*pos = 0;

	for (pos++; pos;) {
		char *cl_name, *cl_value;

		pos = conf_get_pair_equal(conf, pos, &cl_name, &cl_value, ' ');

		if (!cl_name) {
			args = talloc_asprintf_append(args, "%s ", cl_value);
			continue;
		}

		if (streq(cl_name, "initrd")) {
			initrd = cl_value;
			continue;
		}

		if (streq(cl_name, "root")) {
			root = cl_value;
			continue;
		}

		args = talloc_asprintf_append(args, "%s=%s ", cl_name,
			cl_value);
	}

out_add:
	opt->boot_image_file = resolve_path(opt, value,
			conf->dc->device->device_path);

	if (root) {
		opt->boot_args = talloc_asprintf(opt, "root=%s %s", root, args);
		talloc_free(args);
	} else
		opt->boot_args = args;

	if (initrd) {
		opt->initrd_file = resolve_path(opt, initrd,
				conf->dc->device->device_path);

		opt->description = talloc_asprintf(opt, "%s initrd=%s %s",
			value, initrd, opt->boot_args);
	} else
		opt->description = talloc_asprintf(opt, "%s %s", value,
			opt->boot_args);

	conf_strip_str(opt->boot_args);
	conf_strip_str(opt->description);

	discover_context_add_boot_option(conf->dc, opt);
}

static struct conf_global_option kboot_global_options[] = {
	{ .name = "initrd" },
	{ .name = "root" },
	{ .name = "video" },
	{ .name = NULL }
};

static const char *const kboot_conf_files[] = {
	"/kboot.conf",
	"/kboot.cnf",
	"/etc/kboot.conf",
	"/etc/kboot.cnf",
	"/KBOOT.CONF",
	"/KBOOT.CNF",
	"/ETC/KBOOT.CONF",
	"/ETC/KBOOT.CNF",
	NULL
};

static const char *const kboot_ignored_names[] = {
	"default",
	"delay",
	"message",
	"timeout",
	NULL
};

static int kboot_parse(struct discover_context *dc, char *buf, int len)
{
	struct conf_context *conf;

	conf = talloc_zero(dc, struct conf_context);

	if (!conf)
		return 0;

	conf->dc = dc;
	conf->global_options = kboot_global_options,
	conf_init_global_options(conf);
	conf->get_pair = conf_get_pair_equal;
	conf->process_pair = kboot_process_pair;
	conf->parser_info = (void *)kboot_ignored_names,

	conf_parse_buf(conf, buf, len);

	talloc_free(conf);
	return 1;
}

struct parser __kboot_parser = {
	.name		= "kboot",
	.parse		= kboot_parse,
	.filenames	= kboot_conf_files,
};
