# Tiny Gnome Terminal
A minimal terminal base on Gnome VTE.

* No splits
* No tabs
* No daemon
* Server side window header hint

## Features:

* **Pop mode** launch a pop up terminal
  ```sh
  $ tgt --pop
  ```
* **stdio mode** pass stdin and stdout to use tgt inside scripts
  ```sh
  $ echo -e "a\nb\n" | tgt --pop --stdio -- fzf | xargs echo
  ```

