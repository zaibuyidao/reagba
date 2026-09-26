# Optional integration check: gem install reapack-index -v 1.2.3
# ruby tests/ReaPackIndex.rb <generated ReaGBA.ext> <temporary index.xml>
require 'reapack/index'

descriptor, output = ARGV
abort 'Pass the generated descriptor and a temporary index output path' unless output
index = ReaPack::Index.new(output)
index.name = 'zaibuyidao Scripts'
index.commit = 'a' * 40 # Represents the PUBLIC ReaScripts commit, not the source repo.
index.strict = true
index.amend = true
index.scan('Modules/ReaGBA/ReaGBA.ext', File.read(descriptor, encoding: 'UTF-8'))
index.write!

doc = Nokogiri::XML(File.read(output))
package = doc.at_xpath('/index/category[@name="Modules/ReaGBA"]/reapack[@name="ReaGBA.ext"]')
raise 'Not an extension package' unless package && package['type'] == 'extension'
sources = package.xpath('version/source')
platforms = %w[win64 darwin64 darwin-arm64 linux64 linux-aarch64]
raise 'Unexpected platform set' unless sources.map { |s| s['platform'] || 'all' }.uniq.sort == (platforms + ['all']).sort
platforms.each do |platform|
  rows = sources.select { |s| s['platform'] == platform }
  native = rows.select { |s| s['type'] == 'extension' }
  raise 'Expected one flat UserPlugins binary per platform' unless native.size == 1 && !native[0]['file'].include?('/')
  raise 'Unexpected platform-specific files' unless rows.size == 1
end
web = sources.select { |s| s['platform'].nil? }
raise 'Unexpected web assets' unless web.map { |s| s['file'] }.sort == %w[web/zaibuyidao_ReaGBA.lua web/index.html web/style.css web/i18n.js web/app.js].sort
raise 'Web assets must install in Scripts' unless web.all? { |s| s['type'] == 'script' }
raise 'Unexpected source count' unless sources.size == 10
sources.each do |source|
  raise 'Files must not register Lua actions' if source['main']
  raise 'Unexpected private, floating or malformed URL' unless source.text.start_with?('https://raw.githubusercontent.com/zaibuyidao/ReaScripts/' + index.commit + '/Modules/ReaGBA/')
  raise 'Descriptor must not install itself' if source['file'].end_with?('.ext')
end
puts "ReaPack indexer passed: #{platforms.size} platforms, #{sources.size} sources, pinned public URLs, core and web assets"
