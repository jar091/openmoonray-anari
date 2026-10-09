# Third-party software

openmoonray-anari is licensed Apache-2.0 (see [LICENSE](LICENSE)).

Milestone 0 links against nothing third-party. The table below records the
licenses of the pinned dependencies that later milestones will build against;
it is completed (including transitive MoonRay dependencies such as TBB,
OpenImageIO, OpenEXR, Embree, ISPC runtime) during the packaging milestone's
license audit.

| Component | License | Usage |
|---|---|---|
| OpenMoonRay (scene_rdl2, moonray, moonshine, …) | Apache-2.0 | rendering backend (from Milestone 1) |
| ANARI-SDK | Apache-2.0 | device SDK, helium foundation (from Milestone 5) |
| PyNARI | Apache-2.0 | Python testing/examples (from Milestone 7) |
| MoonRay transitive dependencies (TBB, OpenImageIO, OpenEXR, Embree, …) | various | audited in Milestone 14 |
