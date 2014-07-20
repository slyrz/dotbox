# dotbox

Gives you a fresh new home to experiment with your or other people's dotfiles.

## Usage

The `dotbox` command starts a shell in a temporary new home directory.

    dotbox path/to/directory

Running the above command would open a shell where every program uses `path/to/directory`
as your home directory. This way you can edit and test dotfiles without modifying your actual
home directory.

Note: If you want to start X programs, place a copy of `~/.Xauthority` in your new home.

### License

dotbox is released under MIT license.
You can find a copy of the MIT License in the [LICENSE](./LICENSE) file.
