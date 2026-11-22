#!/bin/sh

rm -Rf "/Applications/Mozc"
# floatype: its own folder too, so that a link the new version no longer
# puts there (ConfigDialog.app, before Settings.app) does not stay behind.
rm -Rf "/Applications/floatype"
/usr/bin/true
