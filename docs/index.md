# CSSE4011 SDK Documentation

Welcome to the **CSSE4011 SDK Docs**.

This site contains documentation and examples for the SDK used in the
[CSSE4011](https://uq.edu.au/) Embedded Systems course at UQ. It is designed
to:

- Standardise the Zephyr environment for students.
- Provide a stable base with pinned Zephyr versions.
- Include course-specific drivers, code, and teaching materials.

This documentation assumes that you have set up a development environment using
the [CSSE4011 Student Workspace][csse4011-student]. If you have not done that
yet, refer to the [README][csse4011-student] there.

!!! warning
    This SDK is **not** intended for general production use — it is
    tailored for the CSSE4011 course.

## Structure of the SDK

- **`zephyr/`** – Zephyr RTOS core and modules.
- **`modules/`** – Additional Zephyr modules and drivers.
- **`csse4011-sdk/`** – Course-specific samples and configurations.
- **`firmware/`** – Your workspace for prac solutions and assignments.

## Where to Start

- Read the [Pracs Overview](pracs/index.md) to find your current prac
  instructions.
- Check out the [Samples](samples/index.md) for minimal working examples.
- See [Reference](reference/index.md) for course-specific tips and board info.


[csse4011-student]:https://github.com/skwort/csse4011-student
