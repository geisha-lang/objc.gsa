# objc.gsa

Native Objective-C Runtime & Apple Blocks ABI bridge for Geisha (`macOS` / `iOS` / `iOS Simulator`).

## Features

- **GC-managed Objective-C objects**: `Id a`, `idRetain`, `idAdopt`, `withId`, `withAutoreleasePool`
- **`NSString` bridging**: `nsString`, `fromNSString`, `fromNSStringPtr`
- **Dynamic ObjC subclassing & delegates**: `objcAllocateClass`, `objcAddMethod`, `objcAddProtocol`, `objcRegisterClass` (pair with `extern init` and `cFnPtr` to register `NSApplicationDelegate`, `UIInputViewController`, `IMKInputController`, etc. before `main`)
- **Auto-releasing associated GC handles**: `objcSetAssocHandle`, `objcGetAssocHandle` (releases the Geisha GC root automatically when ObjC `dealloc` runs)
- **Apple Objective-C Blocks ABI**: `objcBlock0`, `objcBlock1Ptr`, `objcBlock2Ptr`, `objcBlock1I64`, `objcBlock1Bool`, `objcBlockCustom` (refcounted `_Block_copy` / `_Block_release`, compatible with `libdispatch`, `AppKit`, `UIKit`, `ScreenCaptureKit`, `AVFoundation`, etc.)
- **Typed `objc_msgSend` helpers**: `msgSend0`, `msgSend1Ptr`, `msgSendVoid1Ptr`, `msgSendInt0`, etc. (for custom `extern data` struct signatures like `CGRect`, declare your own `extern "objc_msgSend" def ...`)

## Usage

In `geisha.toml`:

```toml
[deps]
objc = { path = "../objc.gsa" }
```

```geisha
use objc.{withAutoreleasePool, nsString, fromNSString, objcBlock0}

def main(): Void = withAutoreleasePool(() -> {
    printLn(fromNSString(nsString("Hello from Geisha + Foundation")))
})
```
