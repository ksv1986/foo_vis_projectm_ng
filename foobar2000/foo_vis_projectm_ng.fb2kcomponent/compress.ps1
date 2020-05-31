# Compress-Archive only accepts .zip file names, so need to rename explicitly

$dll = $args[0]
$zip = $([System.IO.Path]::ChangeExtension($dll, ".zip"))
$foo = $([System.IO.Path]::ChangeExtension($dll, ".fb2k-component"))

Compress-Archive -Path $dll -DestinationPath $zip -Force
Move-Item -Path $zip -Destination $foo -Force
