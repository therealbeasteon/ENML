# osapp

`osapp` contains the small runtime protocol shared by App Manager and launched ENML applications.

M1.3 exposes only the bootstrap record needed to prove that a newly exec'd process received its supervisor-issued `PeerIdentity`, fresh `ApplicationInstanceId`, and immutable package-generation binding. It is not yet the public application framework or lifecycle API.
