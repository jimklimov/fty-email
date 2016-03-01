Format:         1.0
Source:         agent-smtp
Version:        0.1.0-1
Binary:         libagent-smtp0, agent-smtp-dev
Architecture:   any all
Maintainer:     John Doe <John.Doe@example.com>
Standards-Version: 3.9.5
Build-Depends: bison, debhelper (>= 8),
    pkg-config,
    automake,
    autoconf,
    libtool,
    libzmq4-dev,
    uuid-dev,
    libczmq-dev,
    libmlm-dev,
    libbiosproto-dev,
    libcxxtools-dev,
    dh-autoreconf

Package-List:
 libagent-smtp0 deb net optional arch=any
 agent-smtp-dev deb libdevel optional arch=any

