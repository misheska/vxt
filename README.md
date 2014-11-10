This source tree contains the compilable VxT source.  The xen subtree builds against the Citrix/XenServer product.  The experimental KVM tree builds against the Fedora 12 kernel.

The experimental KVM tree is a snapshot of a development tree.  Many portions are under construction but the code downloads into the linux kernel, and provides a PCI/VxT card to a Linux guest through a modified qemu-kvm binary.  The VxT code also provides a working driver.  The code is experimental and not product level but it provides a glimpse of the ongoing development of VxT.

Experimental KVM/VxT runs on Fedora-12.  The security software on Fedora-12 will require the following adjustments.


    contents of local.te

    module local 1.0;

    require {
            class file { execmod };
            class file { open read write create };
            class file { read write };
            class chr_file { open read write ioctl };
            class dir { open read write add_name execute };
            type svirt_t;
            type clock_device_t;
            type device_t;
            type usr_t;
            type unconfined_t;
            type lib_t;
    }


    allow svirt_t clock_device_t:chr_file { open ioctl read write };
    allow svirt_t device_t:chr_file { open ioctl read write };
    allow svirt_t usr_t:dir { open read write add_name execute };
    allow svirt_t usr_t:file { open read write create };
    allow svirt_t usr_t:file { read write };
    allow unconfined_t lib_t:file { execmod };



Run the following commands to upload the needed permissions:

    checkmodule -M -m -o local.mod local.te
    semodule_package -o local.pp -m local.mod
    semodule -i local.pp







To run and compile qemu-kvm try the following:

If you want to try the vanilla qemu-kvm first:
    Untar qemu-kvm-0.11.0.tar.gz found in /root/rpmbuild/SOURCES
 
Inspect the qemu.spec file in /root/rpmbuild/SPECS
 
Run the following command before trying to build
 
    ./configure --target-list=x86_64-softmmu --audio-drv-list=pa,sdl.alsa,oss -disable-strip --extra-ldflags="-Wl,--build-id"     --disable-xen
 
.... yum install the needed audio driver libraries
 
    ./make  V=1 VL_LDFLAGS=Wl, --build-id
 
    mv /usr/bin/qemu-kvm /usr/bin/qemu-kvm.orig
 
    cp x86_64-softmmu/qemu-system-x86_64 /usr/bin/qemu-kvm
 
    getfattr -n security.selinux /usr/bin/qemu-kvm.orig
 
    setfattr -n security.selinux -v /usr/bin/qemu-kvm -v "system_u:object_r:qemu_exec_t:s0\000" /usr/bin/qemu-kvm
 

