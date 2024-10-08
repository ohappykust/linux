// SPDX-License-Identifier: GPL-2.0
/*
 * security/tomoyo/load_policy.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include "common.h"

#ifndef CONFIG_SECURITY_TOMOYO_OMIT_USERSPACE_LOADER

/*
 * Path to the policy loader. (default = CONFIG_SECURITY_TOMOYO_POLICY_LOADER)
 */
static const char *tomoyo_loader;

/**
 * tomoyo_loader_setup - Set policy loader.
 *
 * @str: Program to use as a policy loader (e.g. /sbin/tomoyo-init ).
 *
 * Returns 0.
 */
static int __init tomoyo_loader_setup(char *str)
{
	tomoyo_loader = str;
	return 1;
}

__setup("TOMOYO_loader=", tomoyo_loader_setup);

/**
 * tomoyo_policy_loader_exists - Check whether /sbin/tomoyo-init exists.
 *
 * Returns true if /sbin/tomoyo-init exists, false otherwise.
 */
static bool tomoyo_policy_loader_exists(void)
{
	struct path path;

	if (!tomoyo_loader)
		tomoyo_loader = CONFIG_SECURITY_TOMOYO_POLICY_LOADER;
	if (kern_path(tomoyo_loader, LOOKUP_FOLLOW, &path)) {
		pr_info("Not activating Mandatory Access Control as %s does not exist.\n",
			tomoyo_loader);
		return false;
	}
	path_put(&path);
	return true;
}

/*
 * Path to the trigger. (default = CONFIG_SECURITY_TOMOYO_ACTIVATION_TRIGGER)
 */
static const char *tomoyo_trigger;

/**
 * tomoyo_trigger_setup - Set trigger for activation.
 *
 * @str: Program to use as an activation trigger (e.g. /sbin/init ).
 *
 * Returns 0.
 */
static int __init tomoyo_trigger_setup(char *str)
{
	tomoyo_trigger = str;
	return 1;
}

__setup("TOMOYO_trigger=", tomoyo_trigger_setup);

/**
 * tomoyo_load_policy - Run external policy loader to load policy.
 *
 * @filename: The program about to start.
 *
 * This function checks whether @filename is /sbin/init , and if so
 * invoke /sbin/tomoyo-init and wait for the termination of /sbin/tomoyo-init
 * and then continues invocation of /sbin/init.
 * /sbin/tomoyo-init reads policy files in /etc/tomoyo/ directory and
 * writes to /sys/kernel/security/tomoyo/ interfaces.
 *
 * Returns nothing.
 */
void tomoyo_load_policy(const char *filename)
{
	static bool done;
	char *argv[2];
	char *envp[3];

	if (tomoyo_policy_loaded || done)
		return;
	if (!tomoyo_trigger)
		tomoyo_trigger = CONFIG_SECURITY_TOMOYO_ACTIVATION_TRIGGER;
	if (strcmp(filename, tomoyo_trigger))
		return;
	if (!tomoyo_policy_loader_exists())
		return;
	done = true;
#ifdef CONFIG_SECURITY_TOMOYO_LKM
	/* Load tomoyo.ko if not yet loaded. */
	if (!tomoyo_ops.check_profile)
		request_module("tomoyo");
	/* Check if tomoyo.ko was successfully loaded. */
	if (!tomoyo_ops.check_profile)
		panic("Failed to load tomoyo module.");
#endif
	pr_info("Calling %s to load policy. Please wait.\n", tomoyo_loader);
	argv[0] = (char *) tomoyo_loader;
	argv[1] = NULL;
	envp[0] = "HOME=/";
	envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[2] = NULL;
	call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
#ifdef CONFIG_SECURITY_TOMOYO_LKM
	tomoyo_ops.check_profile();
#else
	tomoyo_check_profile();
#endif
}

#endif
