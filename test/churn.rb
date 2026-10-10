#!/usr/bin/env ruby
#
# Concurrent-churn driver for the acceptance acceptor (README, "Exercising the
# concurrent paths"; #15, #94). Workers repeatedly connect to `at`, pick one of
# the sessions in the configuration and either log on and off cleanly, log on
# and reset the connection, or send half a logon and close. Two workers on the
# same session race each other's logons, which is the point: this is what
# drives the accept path, the session registry and the per-connection threads
# concurrently, where the acceptance definitions use one connection at a time.
#
# It judges only that `at` kept accepting connections. Whether the engine
# raced is for the sanitizer `at` runs under to say: run it through
# runchurn.sh, which fails when `at` exits non-zero.
#
#   ruby churn.rb <host> <port> <cfg> [workers] [seconds]

require 'socket'

host, port, cfg = ARGV[0], ARGV[1].to_i, ARGV[2]
workers = (ARGV[3] || 16).to_i
seconds = (ARGV[4] || 60).to_i
unless host && port > 0 && cfg
  warn 'usage: churn.rb <host> <port> <cfg> [workers] [seconds]'
  exit 2
end

# Sessions as the acceptor sees them; the driver takes the other side.
def sessions(cfg)
  defaults = {}
  list = []
  section = nil
  File.foreach(cfg) do |line|
    line = line.strip
    next if line.empty? || line.start_with?('#')
    if line == '[DEFAULT]'
      section = defaults
    elsif line == '[SESSION]'
      section = {}
      list << section
    elsif section && line.include?('=')
      key, value = line.split('=', 2)
      section[key] = value
    end
  end
  list.map { |s| defaults.merge(s) }
end

def message(begin_string, fields)
  body = fields.map { |tag, value| "#{tag}=#{value}\x01" }.join
  text = "8=#{begin_string}\x019=#{body.bytesize}\x01#{body}"
  "#{text}10=#{format('%03d', text.bytes.sum % 256)}\x01"
end

def now
  Time.now.utc.strftime('%Y%m%d-%H:%M:%S.%L')
end

def logon(session)
  fields = [[35, 'A'], [34, 1], [49, session['TargetCompID']], [52, now], [56, session['SenderCompID']],
            [98, 0], [108, 30], [141, 'Y']]
  fields << [1137, 7] if session['BeginString'] == 'FIXT.1.1'
  message(session['BeginString'], fields)
end

def logout(session)
  message(session['BeginString'],
          [[35, '5'], [34, 2], [49, session['TargetCompID']], [52, now], [56, session['SenderCompID']]])
end

# Reads until a message of the given type arrives, the peer closes, or the
# deadline passes.
def await(socket, type, timeout)
  buffer = +''
  deadline = Time.now + timeout
  while Time.now < deadline
    return false unless socket.wait_readable([deadline - Time.now, 0].max)
    chunk = socket.read_nonblock(4096, exception: false)
    return false if chunk.nil?
    next if chunk == :wait_readable
    buffer << chunk
    return true if buffer.include?("\x0135=#{type}\x01")
  end
  false
end

all = sessions(cfg)
stop_at = Time.now + seconds
counts = Hash.new(0)
lock = Mutex.new
count = ->(key) { lock.synchronize { counts[key] += 1 } }

threads = Array.new(workers) do |w|
  Thread.new do
    random = Random.new(w)
    while Time.now < stop_at
      session = all[random.rand(all.size)]
      mode = %i[clean reset partial][random.rand(3)]
      begin
        socket = TCPSocket.new(host, port)
      rescue SystemCallError => e
        count.call(:"refused #{e.class.name.split('::').last}")
        sleep 0.1
        next
      end
      begin
        case mode
        when :clean
          # One second, not longer: a session another worker holds is not
          # answered, and the threaded transport holds such a connection for up
          # to 5 s waiting for the session to free up (ThreadedSocketConnection::
          # setSession). Giving up closes the connection inside that wait, which
          # is itself a path worth racing.
          socket.write(logon(session))
          if await(socket, 'A', 1)
            count.call(:logged_on)
            socket.write(logout(session))
            await(socket, '5', 5)
          end
        when :reset
          socket.write(logon(session))
          await(socket, 'A', random.rand * 0.2)
          socket.setsockopt(Socket::SOL_SOCKET, Socket::SO_LINGER, [1, 0].pack('ii'))
        when :partial
          text = logon(session)
          socket.write(text[0, random.rand(1...text.bytesize)])
        end
        count.call(mode)
      rescue SystemCallError, IOError
        count.call(:"#{mode} dropped by at")
      ensure
        socket.close unless socket.closed?
      end
    end
  end
end
threads.each(&:join)

counts.sort.each { |key, n| puts format('%-24s %d', key, n) }
refused = counts.select { |key, _| key.to_s.start_with?('refused') }.values.sum
if refused.positive?
  puts "FAILED: at refused #{refused} connections"
  exit 1
end
puts "#{workers} workers for #{seconds}s against #{all.size} sessions"
