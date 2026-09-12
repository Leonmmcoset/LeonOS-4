#ifndef LEONOS_ACCOUNT_STORE_H
#define LEONOS_ACCOUNT_STORE_H

/* etc_fd must name a root-owned directory not writable by group or others.
 * The four contents are passwd, shadow, group and gshadow, in that order.
 * Errors after the durable commit point can leave an update awaiting recovery. */
int leonos_account_store_commit(int etc_fd, const char *const contents[4]);
int leonos_account_store_recover(int etc_fd);

#endif
