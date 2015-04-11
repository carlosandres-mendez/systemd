/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2013 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <unistd.h>
#include <stdlib.h>

#include "efivars.h"
#include "path-util.h"
#include "util.h"
#include "mkdir.h"
#include "virt.h"
#include "generator.h"
#include "special.h"

static const char *arg_dest = "/tmp";

int main(int argc, char *argv[]) {
        _cleanup_free_ char *what = NULL;
        _cleanup_fclose_ FILE *f = NULL;
        int r = EXIT_SUCCESS;
        sd_id128_t id;
        char *name;

        if (argc > 1 && argc != 4) {
                log_error("This program takes three or no arguments.");
                return EXIT_FAILURE;
        }

        if (argc > 1)
                arg_dest = argv[3];

        log_set_target(LOG_TARGET_SAFE);
        log_parse_environment();
        log_open();

        umask(0022);

        if (in_initrd()) {
                log_debug("In initrd, exiting.");
                return EXIT_SUCCESS;
        }

        if (detect_container(NULL) > 0) {
                log_debug("In a container, exiting.");
                return EXIT_SUCCESS;
        }

        if (!is_efi_boot()) {
                log_debug("Not an EFI boot, exiting.");
                return EXIT_SUCCESS;
        }

        r = path_is_mount_point("/boot", true);
        if (r == -ENOENT)
                log_debug("/boot does not exist, continuing.");
        else if (r <= 0 && dir_is_empty("/boot") <= 0) {
                log_debug("/boot already populated, exiting.");
                return EXIT_SUCCESS;
        }

        r = efi_loader_get_device_part_uuid(&id);
        if (r == -ENOENT) {
                log_debug("EFI loader partition unknown, exiting.");
                return EXIT_SUCCESS;
        } else if (r < 0) {
                log_error_errno(r, "Failed to read ESP partition UUID: %m");
                return EXIT_FAILURE;
        }

        name = strjoina(arg_dest, "/boot.mount");
        f = fopen(name, "wxe");
        if (!f) {
                log_error_errno(errno, "Failed to create mount unit file %s: %m", name);
                return EXIT_FAILURE;
        }

        r = asprintf(&what,
                     "/dev/disk/by-partuuid/%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                     SD_ID128_FORMAT_VAL(id));
        if (r < 0) {
                log_oom();
                return EXIT_FAILURE;
        }

        fprintf(f,
                "# Automatially generated by systemd-efi-boot-generator\n\n"
                "[Unit]\n"
                "Description=EFI System Partition\n"
                "Documentation=man:systemd-efi-boot-generator(8)\n");

        r = generator_write_fsck_deps(f, arg_dest, what, "/boot", "vfat");
        if (r < 0)
                return EXIT_FAILURE;

        fprintf(f,
                "\n"
                "[Mount]\n"
                "What=%s\n"
                "Where=/boot\n"
                "Type=vfat\n"
                "Options=umask=0077,noauto\n",
                what);

        fflush(f);
        if (ferror(f)) {
                log_error_errno(errno, "Failed to write mount unit file: %m");
                return EXIT_FAILURE;
        }

        name = strjoina(arg_dest, "/boot.automount");
        fclose(f);
        f = fopen(name, "wxe");
        if (!f) {
                log_error_errno(errno, "Failed to create automount unit file %s: %m", name);
                return EXIT_FAILURE;
        }

        fputs("# Automatially generated by systemd-efi-boot-generator\n\n"
              "[Unit]\n"
              "Description=EFI System Partition Automount\n\n"
              "[Automount]\n"
              "Where=/boot\n", f);

        fflush(f);
        if (ferror(f)) {
                log_error_errno(errno, "Failed to write automount unit file: %m");
                return EXIT_FAILURE;
        }

        name = strjoina(arg_dest, "/" SPECIAL_LOCAL_FS_TARGET ".wants/boot.automount");
        mkdir_parents(name, 0755);

        if (symlink("../boot.automount", name) < 0) {
                log_error_errno(errno, "Failed to create symlink %s: %m", name);
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}
