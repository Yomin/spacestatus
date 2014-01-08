
man_MANS = spacestatus.1
EXTRA_DIST = spacestatus.1.in
CLEANFILES = spacestatus.1

SUBST = " \
    s|%PACKAGE%|@PACKAGE@|g; \
    s|%PACKAGE_NAME%|@PACKAGE_NAME@|g; \
    s|%PACKAGE_VERSION%|@PACKAGE_VERSION@|g; \
    s|%PACKAGE_URL%|@PACKAGE_URL@|g; \
    s|%pkgdatadir%|$(pkgdatadir)|g; \
    s|%DATE%|$(shell @DATE@ "+%d %b %Y")|g; \
"

% : %.in
	@SED@ -e $(SUBST) $< >$@
