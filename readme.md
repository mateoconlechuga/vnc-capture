# VncXfer

This is a super lightweight implementation of the VNC protocol, supporting both normal TCP communication and Unix direct sockets.

# Features

It current supports raw encoding and reporting of framebuffer changes, and has all the normal options of the RFB protocol.

Included is a Qt example program for testing. Either run qmake or Qt Creator to build the `.pro` file.

# Goals

This was created because I needed some way to fetch framebuffers on the localhost from running VMs. It's not pretty, but it works.
