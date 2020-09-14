#
# Python interface (and hsot-side simulator) of a LPC810_CryptoMem device.
#
import struct
import binascii

from Crypto.Hash import SHA256, HMAC

#---------------------------------------------------------------------------------------------------
# I2C device driver interface
#
class LPC810_CryptoMem:
    """
    Creates a LPC810 crypto mem driver instance.

    Paramters:
        bus: The I2C bus accessor. The driver assumes that the accessor implements
          a read_i2c_block_data() and write_i2c_block_data() method that is compatible
          with the standard SMBus class.

        i2c_addr: The I2C read address of the device.
    """
    def __init__(self, bus, i2c_addr = 0x20):
        self.bus      = bus
        self.i2c_addr = i2c_addr

    def io_read(self, offset, length):
        """
        Read from the crypto memory
        """
        result = []

        remaining = int(length)
        offset = int(offset)

        while (remaining > 0):
            if remaining < 0x20:
                to_read = remaining
            else:
                to_read = 0x20

            result += self.bus.read_i2c_block_data(self.i2c_addr, offset, to_read)

            offset    += to_read
            remaining -= to_read


        return bytes(result)

    def io_write(self, offset, data):
        """
        Read from the crypto memory
        """
        result = []

        remaining = len(data)
        data_offset = 0
        offset = int(offset)

        while (remaining > 0):
            if remaining < 0x20:
                to_write = remaining
            else:
                to_write = 0x20

            self.bus.write_i2c_block_data(self.i2c_addr, offset, data[data_offset:(data_offset+to_write)])
            offset    += to_write
            remaining -= to_write


    def io_cmd(self, opcode, arg0=0, arg1=0, arg2=0, data=[], rsp_len = 0):
        """
        Execute a command
        """

        if len(data) > 0x50:
            raise ValueError("Invalid data paramter size")

        # Set the output data
        if (len(data) > 0):
            self.io_write(0x00, bytes(data))

        # Send the request
        self.io_write(0x50, [int(arg0), int(arg1), int(arg2), int(opcode)])

        # Poll for the response
        status = 0xFF

        while (status == 0xFF):
            # Poll the resonse
            rsp = self.io_read(0x54, 0x04)
            status = rsp[0]

        return (rsp[0], rsp[1], bytes(self.io_read(0x00, rsp_len)))

    def io_cmd_checked(self, opcode, arg0=0, arg1=0, arg2=0, data=[], rsp_len = 0):
        (status_0, status_1, data) = self.io_cmd(int(opcode), int(arg0), int(arg1), int(arg2), bytes(data), int(rsp_len))

        if ((status_0 != 0xC3) or (status_1 != 0x00)):
            raise RuntimeError("remote i2c command failed (0x%02x 0x%02x)" % (status_0, status_1))


        return data

    def nop(self):
        """
        No-operation
        """
        self.io_cmd_checked(opcode=0x00)

    def pcr(self, idx):
        """
        Reads the current value of a PCR.
        """
        return bytes(self.io_read(0x090 + int(idx) * 0x20, 0x20))

    def extend(self, idx, data):
        """
        Extends a PCR with user-provided DATA
        """
        self.io_cmd_checked(opcode=0xE0, arg0=int(idx), arg1=len(data), data=bytes(data))

    def quote(self, flags=0, data=[]):
        """
        Quotes the current platform status.
        """
        return self.io_cmd_checked(opcode=0xA0, arg0=int(flags), arg1=len(data), data=bytes(data), rsp_len=0x20)


    def device_uid(self):
        """
        Reads the device UID
        """
        return self.io_read(0xF0, 0x10)

    def user_data(self):
        """
        Reads the user-data area.
        """
        return self.io_read(0x70, 0x20)

    def volatile_bits(self):
        """
        Reads the current value of the (lockable) volatile bits
        """
        return struct.unpack("<I", self.io_read(0x058, 0x04))[0]

    def volatile_locks(self):
        """
        Reads the current lock status of the (lockable) volatile bits
        """
        return struct.unpack("<I", self.io_read(0x058, 0x08))[0]

    def ctr(self, idx):
        """
        Reads the current value of the (lockable) volatile bits and locks
        """
        return struct.unpack("<I", self.io_read(0x060 + 0x04 * int(idx), 0x04))[0]

    def increment(self, idx, addend=1):
        """
        Increments a volatile counter
        """
        return self.io_cmd(opcode=0xC0, arg0=int(idx), arg1=int(addend))

    def hkdf(self, seed=[]):
        """
        Derives a key using the HMAC based KDF
        """
        return self.io_cmd_checked(opcode=0xB0, arg0=len(seed), data=bytes(seed), rsp_len=0x20)

#---------------------------------------------------------------------------------------------------
# Host side simulator
#
class LPC810_CryptoMem_Simulator:
    DEFAULT_ROOT_AUTH  = b"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    DEFAULT_USER_AUTH  = b"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    DEFAULT_ROOT_KEY   = b"\x66\x68\x7a\xad\xf8\x62\xbd\x77\x6c\x8f\xc1\x8b\x8e\x9f\x8e\x20\x08\x97\x14\x85\x6e\xe2\x33\xb3\x90\x2a\x59\x1d\x0d\x5f\x29\x25"
    DEFAULT_HKDF_SEED  = b"\xC3\xC3\xC3\xC3\xC3\xC3\xC3\xC3"
    DEFAULT_QUOTE_SEED = b"\x3C\x3C\x3C\x3C\x3C\x3C\x3C\x3C"

    # The UUID of our test device
    DEFAULT_DEVICE_UUID = b"\x38\x20\x04\x16\x02\x84\x1b\xae\xa9\x0d\xd8\x51\x03\x19\x00\xf5"

    def __init__(self, root_key = DEFAULT_ROOT_KEY, quote_seed = DEFAULT_QUOTE_SEED, hkdf_seed = DEFAULT_HKDF_SEED, dev_uuid = DEFAULT_DEVICE_UUID):
        self.root_key  = bytes(root_key)
        self.quote_key = self.derive_device_key(b"QUOT", quote_seed)
        self.hkdf_key  = self.derive_device_key(b"HKDF", hkdf_seed)
        self.dev_uuid  = bytes(dev_uuid)

        self.pcrs = [
            b"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
            b"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
            b"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        ]

        self.nv_user_data = b"\x64\x6f\x6e\x27\x74\x20\x66\x65\x65\x64\x20\x74\x68\x65\x20\x62\x75\x67\x73\x21\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"

        # Volatile bits (and their locks)
        self.volatile_bits = 0
        self.volatile_locks = 0

        # Volatile counters
        self.volatile_ctr0 = 0
        self.volatile_ctr1 = 0


    def derive_device_key(self, ktype, kseed):
        hmac = HMAC.new(self.root_key, digestmod=SHA256)
        hmac.update(bytes(kseed))
        hmac.update(bytes(ktype))
        return bytes(hmac.digest())

    def hkdf(self, seed=[]):
        hmac = HMAC.new(self.hkdf_key, digestmod=SHA256)
        hmac.update(bytes(seed))
        return bytes(hmac.digest())

    def extend(self, index, data):
        md = SHA256.new()
        md.update(bytes(self.pcrs[index]))
        md.update(bytes(data))
        self.pcrs[index] = bytes(md.digest())

    def pcr(self, index):
        return bytes(self.pcrs[index])

    def quote(self, flags=0, data=[]):
        # Construct the header blob
        pcr_mask = int(flags)
        data     = bytes(data)

        hmac = HMAC.new(self.quote_key, digestmod=SHA256)

        # Header
        hmac.update(b"QUOT")
        hmac.update(struct.pack("<I", pcr_mask))
        
        # Device UUID
        if (0 != (pcr_mask & 0x80)):
            hmac.update(self.dev_uuid)

        # Lockable (volatile) bits
        if (0 != (pcr_mask & 0x40)):
            hmac.update(struct.pack("<II", self.volatile_bits, self.volatile_locks))

        # Counters
        if (0 != (pcr_mask & 0x20)):
            hmac.update(struct.pack("<I", self.volatile_ctr0))

        if (0 != (pcr_mask & 0x10)):
            hmac.update(struct.pack("<I", self.volatile_ctr1))

        # If enabled: User data
        if (0 != (pcr_mask & 0x08)):
            hmac.update(self.nv_user_data)

        # And the enabled PCRs                
        if (0 != (pcr_mask & 0x04)):
            hmac.update(self.pcrs[2])

        if (0 != (pcr_mask & 0x02)):
            hmac.update(self.pcrs[1])

        if (0 != (pcr_mask & 0x01)):
            hmac.update(self.pcrs[0])

        if (len(data) > 0):
            hmac.update(data)

        return bytes(hmac.digest())
        

