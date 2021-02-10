#run this after clone of devtips repo, not (yet) using git submodules as they have undesirable limitations on branch maintenance.


#this branch of safely was created to remove ezcpp repo dependence. I think we will undo that soon, via reconciling ezcpp and safely/cppext
git clone -b cortexm --single-branch https://github.com/980f/safely

#our ARM cortexM microcontroller support libs
git clone https://github.com/980f/cortexm


