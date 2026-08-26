# Lean

> A lightweight terminal text editor written in C++.

<p align="center">
  <img src="Screenshots/Lean.png" alt="Lean Editor">
</p>

<p align="center">
  <b>Lean v0.1.2</b><br>
  A small, lightweight terminal-based text editor built from scratch in C++.
</p>

---

## Features

Lean is intentionally simple. No huge framework, fast to setup.

* Edit text files
* Open existing files
* Save files
* Undo and redo
* Copy, cut, and paste
* Select all text
* Mouse text selection
* Mouse-wheel scrolling
* Arrow-key navigation
* Line-number gutter
* Line and column status bar
* Automatic indentation
* Automatic closing of brackets and quotes
* Build C++ programs
* Run programs in a separate terminal
* X11 and Wayland clipboard support

---

## Controls

| Shortcut      | Action      |
| :------------ | :---------- |
| `Ctrl + S`    | Save        |
| `Ctrl + B`    | Build       |
| `Ctrl + R`    | Run         |
| `Ctrl + A`    | Select all  |
| `Ctrl + C`    | Copy        |
| `Ctrl + X`    | Cut         |
| `Ctrl + V`    | Paste       |
| `Ctrl + Z`    | Undo        |
| `Ctrl + Y`    | Redo        |
| `Ctrl + End`  | Exit        |
| `Arrow Keys`  | Navigate    |
| `Backspace`   | Delete      |
| `Enter`       | New line    |
| `Mouse`       | Select text |
| `Mouse Wheel` | Scroll      |

---

## Usage

### Open a file

Run Lean from your terminal:

```bash
./lean <file>
```

For example:

```bash
./lean test.cpp
```

You can also start Lean without opening a file:

```bash
./lean
```

---

### Save

Press:

```text
Ctrl + S
```

If the file has not been saved before, Lean will ask for a filename.

For example:

```text
Save as: hello.cpp
```

Remember to include the file extension.

---

### Build

Press:

```text
Ctrl + B
```

The file must be saved first.

Lean currently uses `g++` to build C++ files.

For example, saving:

```text
hello.cpp
```

will produce:

```text
hello
```

---

### Run

Press:

```text
Ctrl + R
```

The file must be saved and built first.

Lean launches the program in a separate terminal window.

---

## Clipboard Support

Lean supports system clipboard integration through several clipboard utilities.

### X11

Lean supports:

```text
xclip
xsel
```

### Wayland

Lean supports:

```text
wl-clipboard
```

If the required clipboard utility is not installed, Lean will still run normally, but system clipboard integration will not be available.

---

## Screenshots

### Editor

![Lean Editor](Screenshots/Lean.png)

### Running

![Lean Inside Lean](Screenshots/LeanInlean.png)

### Coding

![Lean Running Lean](Screenshots/leanrunninglean.png)

---

## Installation

Lean can be built directly from the source code.
(Or downloaded from the Releases tab if you're only looking for the editor)

Clone the repository:

```bash
git clone <repository-url>
cd Lean
```

Compile Lean with:

```bash
g++ main.cpp -o lean
```

Then run it:

```bash
./lean
```

Or open a file directly:

```bash
./lean test.cpp
```

> Clipboard functionality requires the appropriate clipboard utility for your display server.

---

## Current Status

**Lean v0.1.2**

Lean is still in early development and is evolving over time.

The current version focuses primarily on C++ development and provides the core features needed for a lightweight terminal editing workflow.

Future versions may add:

* Support for additional programming languages
* Search
* More editor commands
* More customization
* Additional navigation features
* Further improvements to editing and selection

---

## License

Lean is licensed under the **Apache License 2.0**.

Copyright © 2026 Emi.

See the [`LICENSE`](LICENSE) file for the full license text.

---

## About

Developed by **Emi**.

**emiowo.dev**

Have an idea, suggestion, or feedback?

Feel free to get in touch through my socials on my website!

Enjoy writing! :3

— Sincerely,
**Emi**
