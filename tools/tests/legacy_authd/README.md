# Retired Authentication Fixtures

These sources preserve the previous working-tree implementation and historical
password/cache regressions. They are not production sources, public headers,
services, or installer payload. No production target may compile this directory.

Production authentication uses Linux-PAM; execution uses upstream sudo/su;
account management uses shadow utilities and the standard four account files.
The historical broker protocol is intentionally unavailable.
