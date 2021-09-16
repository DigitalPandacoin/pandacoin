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
The macOS configuration assumes pandacoind will be set up for the current user.

Configuration
---------------------------------

At a bare minimum, pandacoind requires that the rpcpassword setting be set
when running as a daemon.  If the configuration file does not exist or this
setting is not set, pandacoind will shut down promptly after startup.

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
see `share/examples/pandacoin.conf`.

Paths
---------------------------------

### Linux

All three configurations assume several paths that might need to be adjusted.

Binary:              `/usr/bin/pandacoind`
Configuration file:  `/etc/pandacoin/pandacoin.conf`
Data directory:      `/var/lib/pandacoind`
PID file:            `/var/run/pandacoind/pandacoind.pid` (OpenRC and Upstart) or `/run/pandacoind/pandacoind.pid` (systemd)  
Lock file:           `/var/lock/subsys/pandacoind` (CentOS)

The PID directory (if applicable) and data directory should both be owned by the
pandacoin user and group. It is advised for security reasons to make the
configuration file and data directory only readable by the pandacoin user and
group. Access to pandacoin-cli and other pandacoind rpc clients can then be
controlled by group membership.

NOTE: When using the systemd .service file, the creation of the aforementioned
directories and the setting of their permissions is automatically handled by
systemd. Directories are given a permission of 710, giving the bitcoin group
access to files under it _if_ the files themselves give permission to the
bitcoin group to do so (e.g. when `-sysperms` is specified). This does not allow
for the listing of files under the directory.

NOTE: It is not currently possible to override `datadir` in
`/etc/bitcoin/bitcoin.conf` with the current systemd, OpenRC, and Upstart init
files out-of-the-box. This is because the command line options specified in the
init files take precedence over the configurations in
`/etc/bitcoin/bitcoin.conf`. However, some init systems have their own
configuration mechanisms that would allow for overriding the command line
options specified in the init files (e.g. setting `BITCOIND_DATADIR` for
OpenRC).

### macOS

NOTE: It is not currently possible to override `datadir` in
`/etc/pandacoin/pandacoin.conf` with the current systemd, OpenRC, and Upstart init
files out-of-the-box. This is because the command line options specified in the
init files take precedence over the configurations in
`/etc/pandacoin/pandacoin.conf`. However, some init systems have their own
configuration mechanisms that would allow for overriding the command line
options specified in the init files (e.g. setting `BITCOIND_DATADIR` for
OpenRC).

### macOS

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

NOTE: When installing for systemd in Debian/Ubuntu the .service file needs to be copied to the /lib/systemd/system directory instead.

### OpenRC

Rename pandacoind.openrc to pandacoind and drop it in /etc/init.d.  Double
check ownership and permissions and make it executable.  Test it with
`/etc/init.d/pandacoind start` and configure it to run on startup with
`rc-update add pandacoind`

### Upstart (for Debian/Ubuntu based distributions)

Upstart is the default init system for Debian/Ubuntu versions older than 15.04. If you are using version 15.04 or newer and haven't manually configured upstart you should follow the systemd instructions instead.

Drop pandacoind.conf in /etc/init.  Test by running `service pandacoind start`
it will automatically start on reboot.

NOTE: This script is incompatible with CentOS 5 and Amazon Linux 2014 as they
use old versions of Upstart and do not supply the start-stop-daemon utility.

### CentOS

Copy pandacoind.init to /etc/init.d/pandacoind. Test by running `service pandacoind start`.

Using this script, you can adjust the path and flags to the pandacoind program by
setting the PANDACOIND and FLAGS environment variables in the file
/etc/sysconfig/pandacoind. You can also use the DAEMONOPTS environment variable here.

### macOS

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
