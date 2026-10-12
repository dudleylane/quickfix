# The acceptance-test acceptor, through the Ruby binding: what src/at.cpp does,
# for test/runat.sh with AT set (see test/acceptor.sh).
#   ruby at.rb -f FILE [-t]
require 'optparse'
require 'quickfix_ruby'
require_relative 'at_application'

file = nil
threaded = false
begin
	rest = OptionParser.new do |parser|
		parser.on('-f FILE') { |value| file = value }
		parser.on('-t') { threaded = true }
	end.parse(ARGV)
rescue OptionParser::ParseError
	rest = ['usage']
end
unless file && rest.empty?
	puts 'usage: at.rb -f FILE [-t]'
	exit 1
end

# runat.sh stops the acceptor with SIGTERM and fails the run unless it exits 0.
stopped = false
Signal.trap('TERM') { stopped = true }
Signal.trap('INT') { stopped = true }

settings = Quickfix::SessionSettings.new(file)
application = Application.new
factory = Quickfix::FileStoreFactory.new('store')
acceptorClass = threaded ? Quickfix::ThreadedSocketAcceptorBase : Quickfix::SocketAcceptor
acceptor = acceptorClass.new(application, factory, settings)

acceptor.start
sleep 1 until stopped
acceptor.stop
