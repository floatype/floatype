#!/bin/sh

# floatype: not /Applications/Mozc, which belongs to a Mozc installed beside
# it. Its own folder, so that a link the new version no longer puts there
# (ConfigDialog.app, before Settings.app) does not stay behind.
rm -Rf "/Applications/floatype"
/usr/bin/true
