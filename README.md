# minimal-ext2
A minimal implementation of the EXT2 file system designed as a plugin-in for hobby operating systems.

This is a minimal implementation of the second exteded file system, which was designed for my own Pintos project. Pintos is an educational operating system that was used to teach the OS course in Stanford University, which was then adopted by many other universities in the world. The Pintos OS has a similar file system as the Linux fs, but with a simplified design. The Pintos fs cannot be read or written directly from any other OS, and the file is transferred between the real OS and the experiemental OS via some special mechanism. Therefore the idea of implementing ext2 on Pintos to get rid of such complication.

The compilation of the file system would require standard interfaces to the block device, standard library and kernel synchronisation mechanism, in particular the following:
<ul>
<li>	1. kernel/kmalloc.h </li>
<li>	2. kernel/synch.h </li>
<li>	3. devices/block.h </li>
<li>	4. lib/bitmap.h </li>
<li>	5. Standard C Library </li>
</ul>
These interfaces and codes are already included in the Pintos OS if you are planning to adapt the code, or otherwise you will have to implement them in your own hobby OS.

The code is distributed under BSD Clause-2, you are free to use the code for any purposes as long as the license is not vialated. The authors and contributors will not hold any liabilities to any damages that incur by using this software, so use it on your own risk.
