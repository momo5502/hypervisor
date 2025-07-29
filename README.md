# 🧪 Hypervisor with EPT Hooking Support
![license](https://img.shields.io/github/license/momo5502/hypervisor.svg)
[![build](https://github.com/momo5502/hypervisor/workflows/Build/badge.svg)](https://github.com/momo5502/hypervisor/actions)
[![paypal](https://img.shields.io/badge/PayPal-support-blue.svg?logo=paypal)](https://paypal.me/momo5502)

A lightweight experimental hypervisor that leverages Intel's VT-x virtualization technology to create stealthy memory hooks using EPT (Extended Page Tables). By manipulating second-level address translation, it enables invisible code execution interception that bypasses traditional memory integrity checks.

## Safety Warnings

- **System Instability**: Improper hypervisor implementation can cause BSODs
- **Data Loss Risk**: Always backup important data before testing
- **Ethical Usage**: Only use for legitimate research and educational purposes

## Credits

<a href="https://github.com/ionescu007/SimpleVisor">SimpleVisor</a>  
<a href="https://github.com/Gbps/gbhv/tree/master/gbhv">gbhv</a> 

<a href="https://www.flaticon.com/free-icon/cyber-security_2092663?related_id=2092663&origin=tag" title="cyber security icons">Icon</a>
