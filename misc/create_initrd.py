import struct, time, sys, os, datetime
from ctypes import c_uint32

_IFDIR = 0040000

#struct fileheader {
#	int parent; /* inode of the parent directory; 0 for the root dir */
#	int inode; /* inode of this file/directory; also 0 for the root dir */
#	int mtime;
#	char name[64];
#	uint32 mode; /* orig. file perms & ~0222 - includes S_ISDIR() */
#	uint32 offset; /* # of bytes into initrd file is located. DIRS: 0 (N/A) */
#	uint32 length; /* file: # bytes. dir: # direct child entries...? */
#};
structstr = 'iii64sIII'

# Describes one file or directory on the initrd
class File(object):
	def __init__(self, parent, inode, name, mode, length, path, mtime):
		self.parent = parent
		self.inode = inode
		self.name = name[:63]
		if (self.name != name):
			sys.stderr.write("WARNING: truncated filename \"{0}\" to \"{1}\"".format(name, self.name))
		self.mode = c_uint32(mode & ~0222).value # remove write bit; initrd files are always read only
		self.offset = 0 # set up later
		self.length = length
		self.path = path
		assert mtime >= 0
		assert mtime < 2**31 - 1
		self.mtime = mtime
	def __repr__(self):
		return struct.pack(structstr, self.parent, self.inode, self.mtime, self.name, self.mode, self.offset, self.length)
	def __str__(self):
		return __repr__(self)

# Creates an initrd image from a set of Files (see above)
def create_image(output_path, files):
	if len(files) < 1:
		sys.stderr.write('Error: attempted to write 0 files')
		sys.exit(1)

	f = open(output_path, "w")
	header_fmt = 'i'
	f.write(struct.pack(header_fmt, len(files))) # write the initrd header

# total data bytes
	total = 0

	print 'initrd header length: {0} bytes'.format(struct.calcsize(header_fmt))
	print 'file header length: {0} bytes per file'.format(struct.calcsize(structstr))

	# Write all the headers
	for file in files:
		if (file.mode & _IFDIR) == 0:
			file.offset = 4 + struct.calcsize(structstr) * len(files) + total # first header + all file headers + all previously written file data
		else:
			# Dirs have no data and thus no offset
			file.offset = 0

			# Set file.length to the number of child entries
			children = 0
			for _f in files:
				if _f.parent == file.inode:
					children += 1
			file.length = children

		f.write(file.__repr__())
		readable_date = datetime.datetime.fromtimestamp(file.mtime).strftime('%Y-%m-%d %H:%M:%S')
		print "Wrote file header: parent={0} inode={1} mtime={2} name={3} mode={4} offset={5} length={6} {7}".format(file.parent, file.inode, readable_date, file.name, oct(file.mode), file.offset, file.length, "(# direct children)" if (file.mode & _IFDIR) else "")
		if (file.mode & _IFDIR) == 0:
			# Only files change the offset, not dirs!
			total += file.length

	# Write the file data
	for file in files:
		if (file.mode & _IFDIR):
			continue
		if (file.length == 0):
		 	print 'Skipping write of 0-length file {0}'.format(file.path)
		 	continue

		inf = open(file.path, "r")

		assert f.tell() == file.offset
		f.write(inf.read())
		assert f.tell() == file.offset + file.length

		inf.close()

		print 'Wrote {0} bytes of data for file {1} at offset {2}, real path {3}'.format(file.length, file.name, file.offset, file.path)

	print 'Done creating initrd! Wrote {0} bytes.'.format(f.tell())

	f.close()

def inode_gen():
	i = 1
	while True:
		yield i
		i += 1

def parent_inode(path, files):
	dir = os.path.dirname(path)
	if len(dir) == 1 or dir == 'initrd':
		# root directory, inode 1
		return 1

	for file in files:
		if file.path == dir:
			return file.inode

	raise Exception('parent_node: parent for {0} not found!'.format(path))

def add_entry(found, root, dir):
	st = os.stat(os.path.join(root, dir))
	mode = st.st_mode
	size = st.st_size
	mtime= st.st_mtime
	if (mode & _IFDIR):
		size = 0
	parent_ino = parent_inode(os.path.join(root, dir), found)
	this_inode = inode.next()
	f = File(parent_ino, this_inode, dir, mode, size, os.path.join(root, dir), mtime)
	found.append(f)

if __name__ == '__main__':
	found = []
	inode = inode_gen()

	found.append(File(0, 0, "", _IFDIR, 0, "", 0)) # Add the null entry (inode 0 is invalid in many utils)
	# add the root dir
	add_entry(found, 'initrd', '/')

	# loop through stuff
	for root, subdirs, files in os.walk('initrd', topdown=True):
		for dir in subdirs:
			add_entry(found, root, dir)
		for file in files:
			add_entry(found, root, file)

		# Don't recurse into fat or ext2 in case stuff's mounted there
		# in the host OS; however, the empty directories are added above
		subdirs[:] = [d for d in subdirs if d not in ['ext2', 'fat']]

	create_image('isofiles/boot/initrd.img', found)


#f1 = File(0, 0, '/', 0777 | _IFDIR, 1, None)
#f2 = File(0, 1, 'bin', 0777 | _IFDIR, 1, None)
#f3 = File(1, 2, 'grep', 0755, 0x10402, "/bin/grep")
#f4 = File(1, 3, 'ls', 0755, 0x123, "/bin/ls")
#create_image('out.img', [f1, f2, f3, f4])
