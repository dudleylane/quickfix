# The environment the binding tests start their child processes with. The
# children find quickfix through RUBYLIB, as the suite does, so they load the
# module test.sh puts first -- CMake's lib/ruby before make_ruby.sh's here
# (#118). A -I would come before RUBYLIB and load this directory's module, stale
# or not, whatever the suite itself loaded. This directory comes last, for a
# suite run without test.sh.
module ChildLoadPath
	def self.environment
		{ 'RUBYLIB' => [ENV['RUBYLIB'], File.expand_path('..', __dir__)].compact.join(File::PATH_SEPARATOR) }
	end
end
