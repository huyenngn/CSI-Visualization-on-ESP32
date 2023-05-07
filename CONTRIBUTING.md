# How to Contribute
Read the [README.md](README.md) before contributing.

## Initial setup

Before starting make sure you have the correct access rights by following [this](https://docs.github.com/authentication/connecting-to-github-with-ssh/generating-a-new-ssh-key-and-adding-it-to-the-ssh-agent) guide and that your git configuration matches that of your GitHub account:
```
git config --global user.name [your-name]
git config --global user.email [github-email]
```

Next, clone this git repository:
```
git clone --recurse git@github.com:huyenngn/CSI-Visualization-on-ESP32.git
```
Finally, you can checkout the project's main branch:
```
cd CSI-Visualization-on-ESP32
```

## Git cheat sheet

You should only make changes to your own branch:

* `git branch` - List all branches. a * will appear next to the currently active branch
* `git branch [branch-name]` - Create a new branch at the current commit
* `git checkout [branch-name]` - Switch to your branch

To commit your changes to the current branch:

0. `git pull --rebase` - (optional) It is best practice to always rebase your local commits when you pull before pushing them.
1. `git add [file]` - Add a file as it looks now to your next commit (stage) OR `git add .` - Add all files
2. `git commit -m “[descriptive message]”` - Commit your staged content as a new commit snapshot
3. `git push` - Transmit local branch commits to the remote repository branch

To merge your developemental branch into the project's main branch (You should only do that after consulting all team members):

1. `git checkout [branch-name]` - Checkout your branch
2. `git rebase main` - Rebase main branch into your current branch
1. `git checkout main` - Checkout main branch
3. `git rebase [branch-name]` - Rebase your branch into main branch