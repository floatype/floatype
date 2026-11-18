#!/bin/sh
CONSOLE_USER=`/usr/bin/stat -f%Su /dev/console`
/usr/bin/sudo -u $CONSOLE_USER /usr/bin/killall floatypeConverter > /dev/null

if [ -n "$CONSOLE_USER" ] && [ "$CONSOLE_USER" != "root" ] && [ "$COMMAND_LINE_INSTALL" != "1" ]; then
  /usr/bin/sudo -u "$CONSOLE_USER" "/Library/Input Methods/floatype.app/Contents/MacOS/floatype" --register_input_source > /dev/null 2>&1 || true
fi

/usr/bin/true
