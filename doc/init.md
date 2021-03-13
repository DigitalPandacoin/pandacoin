Sample init scripts and service configuration for pandacoind
==========================================================

Sample scripts and configuration files for systemd, Upstart and OpenRC
can be found in the contrib/init folder.

    contrib/init/pandacoind.service:    systemd service unit configuration
    contrib/init/pandacoind.openrc:     OpenRC compatible SysV style init script
    contrib/init/pandacoind.openrcconf: OpenRC conf.d file
    contrib/init/pandacoind.conf:       Upstart service configuration file
    contrib/init/pandacoind.init:       CentOS compatible SysV style init script

Service User
---------------------------------

All three Linux startup configurations assume the existence of a "pandacoin" user
and group.  They must be created before attempting to use these scripts.
The OS X configuration assumes pandacoind will be set up for the current user.

Configuration
---------------------------------

At a bare minimum, pandacoind requires that the rpcpassword setting be set
when running as a daemon.  If the configuration file does not exist or this
setting is not set, pandacoind will shutdown promptly after startup.

This password does not have to be remembered or typed as it is mostly used
as a fixed token that pandacoind and client programs read from the configuration
file, however it is recommended that a strong and secure password be used
as this password is security critical to securing the wallet should the
wallet be enabled.

If pandacoind is run with the "-server" flag (set by default), and no rpcpassword is set,
it will use a special cookie file for authentication. The cookie is generated with random
content when the daemon starts, and deleted when it exits. Read access to this file
controls who can access it through RPC.

By default the cookie is stored in the data directory, but it's location can be overridden
with the option '-rpccookiefile'.

This allows for running pandacoind without having to do any manual configuration.

`conf`, `pid`, and `wallet` accept relative paths which are interpreted as
relative to the data directory. `wallet` *only* supports relative paths.

For an example configuration file that describes the configuration settings,
see `contrib/debian/examples/pandacoin.conf`.

Paths
---------------------------------

### Linux

All three configurations assume several paths that might need to be adjusted.

Binary:              `/usr/bin/pandacoind`  
Configuration file:  `/etc/pandacoin/pandacoin.conf`  
Data directory:      `/var/lib/pandacoind`  
PID file:            `/var/run/pandacoind/pandacoind.pid` (OpenRC and Upstart) or `/var/lib/pandacoind/pandacoind.pid` (systemd)  
Lock file:           `/var/lock/subsys/pandacoind` (CentOS)  

The configuration file, PID directory (if applicable) and data directory
should all be owned by the pandacoin user and group.  It is advised for security
reasons to make the configuration file and data directory only readable by the
pandacoin user and group.  Access to pandacoin-cli and other pandacoind rpc clients
can then be controlled by group membership.

### Mac OS X

Binary:              `/usr/local/bin/pandacoind`  
Configuration file:  `~/Library/Application Support/Pandacoin/pandacoin.conf`  
Data directory:      `~/Library/Application Support/Pandacoin`  
Lock file:           `~/Library/Application Support/Pandacoin/.lock`  

Installing Service Configuration
-----------------------------------

### systemd

Installing this .service file consists of just copying it to
/usr/lib/systemd/system directory, followed by the command
`systemctl daemon-reload` in order to update running systemd configuration.

To test, run `systemctl start pandacoind` and to enable for system startup run
`systemctl enable pandacoind`

### OpenRC

Rename pandacoind.openrc to pandacoind and drop it in /etc/init.d.  Double
check ownership and permissions and make it executable.  Test it with
`/etc/init.d/pandacoind start` and configure it to run on startup with
`rc-update add pandacoind`

### Upstart (for Debian/Ubuntu based distributions)

Drop pandacoind.conf in /etc/init.  Test by running `service pandacoind start`
it will automatically start on reboot.

NOTE: This script is incompatible with CentOS 5 and Amazon Linux 2014 as they
use old versions of Upstart and do not supply the start-stop-daemon utility.

### CentOS

Copy pandacoind.init to /etc/init.d/pandacoind. Test by running `service pandacoind start`.

Using this script, you can adjust the path and flags to the pandacoind program by
setting the PANDACOIND and FLAGS environment variables in the file
/etc/sysconfig/pandacoind. You can also use the DAEMONOPTS environment variable here.

### Mac OS X

Copy org.pandacoin.pandacoind.plist into ~/Library/LaunchAgents. Load the launch agent by
running `launchctl load ~/Library/LaunchAgents/org.pandacoin.pandacoind.plist`.

This Launch Agent will cause pandacoind to start whenever the user logs in.

NOTE: This approach is intended for those wanting to run pandacoind as the current user.
You will need to modify org.pandacoin.pandacoind.plist if you intend to use it as a
Launch Daemon with a dedicated pandacoin user.

Auto-respawn
-----------------------------------

Auto respawning is currently only configured for Upstart and systemd.
Reasonable defaults have been chosen but YMMV.
