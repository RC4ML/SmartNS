# Artifact Appendix

## AE-only machine access details

This section is intended for AE reviewers using our temporary evaluation machines.

**Important:** both USER and PASSWORD are `eurosys26`.

We created a sudo user `eurosys26` on all machines and disabled password-based SSH login for security reasons. Use the following process to connect:

1. Download the private key `eurosys26_id_ed25519` from the submission website.
2. Get the jump server domain name (referred to as `jump` below) from the submission website. We provide both IPv4 and IPv6 servers (IPv6 recommended).
3. Connect with:

~~~bash
ssh eurosys26@js.v4.rc4ml.org -p <PORT> -i eurosys26_id_ed25519
~~~

4. Start testing.
5. If you have any questions, please contact us.

You can find an NFS folder named `nfs`. It is located on `Host1` and shared with `Host2`, `BF1`, and `BF2`, so cloning SmartNS into this folder enables a synchronized workflow across machines.
