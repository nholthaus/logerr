# gitInfo Library
Provides information about a project from git

Several `const std::string` are defined in the namespace `git`, which include such information as branch, commit, tags, etc.
## Usage

```c++
std::string branch = git::branch;   // the name of the current git branch
```