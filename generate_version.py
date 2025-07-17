#!/usr/bin/env python3

import subprocess
import os

def get_git_hash():
    try:
        # Get short git hash
        result = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], 
                              capture_output=True, text=True, cwd=os.path.dirname(os.path.abspath(__file__)))
        if result.returncode == 0:
            return result.stdout.strip()
        else:
            return "unknown"
    except:
        return "unknown"

def get_git_branch():
    try:
        # Get current branch name
        result = subprocess.run(['git', 'rev-parse', '--abbrev-ref', 'HEAD'], 
                              capture_output=True, text=True, cwd=os.path.dirname(os.path.abspath(__file__)))
        if result.returncode == 0:
            return result.stdout.strip()
        else:
            return "unknown"
    except:
        return "unknown"

def main():
    git_hash = get_git_hash()
    git_branch = get_git_branch()
    
    # Generate version header
    version_content = f"""#ifndef VERSION_H
#define VERSION_H

#define GIT_HASH "{git_hash}"
#define GIT_BRANCH "{git_branch}"
#define BUILD_VERSION "{git_hash}"

#endif // VERSION_H
"""
    
    # Write to include directory
    include_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "include")
    os.makedirs(include_dir, exist_ok=True)
    
    with open(os.path.join(include_dir, "version.h"), "w") as f:
        f.write(version_content)
    
    print(f"Generated version.h with hash: {git_hash}")

if __name__ == "__main__":
    main()
