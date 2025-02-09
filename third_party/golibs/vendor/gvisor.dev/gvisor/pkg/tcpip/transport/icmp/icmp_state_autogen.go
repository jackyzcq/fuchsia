// automatically generated by stateify.

package icmp

import (
	"gvisor.dev/gvisor/pkg/state"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
)

func (p *icmpPacket) StateTypeName() string {
	return "pkg/tcpip/transport/icmp.icmpPacket"
}

func (p *icmpPacket) StateFields() []string {
	return []string{
		"icmpPacketEntry",
		"senderAddress",
		"data",
		"receivedAt",
	}
}

func (p *icmpPacket) beforeSave() {}

// +checklocksignore
func (p *icmpPacket) StateSave(stateSinkObject state.Sink) {
	p.beforeSave()
	var dataValue buffer.VectorisedView
	dataValue = p.saveData()
	stateSinkObject.SaveValue(2, dataValue)
	var receivedAtValue int64
	receivedAtValue = p.saveReceivedAt()
	stateSinkObject.SaveValue(3, receivedAtValue)
	stateSinkObject.Save(0, &p.icmpPacketEntry)
	stateSinkObject.Save(1, &p.senderAddress)
}

func (p *icmpPacket) afterLoad() {}

// +checklocksignore
func (p *icmpPacket) StateLoad(stateSourceObject state.Source) {
	stateSourceObject.Load(0, &p.icmpPacketEntry)
	stateSourceObject.Load(1, &p.senderAddress)
	stateSourceObject.LoadValue(2, new(buffer.VectorisedView), func(y interface{}) { p.loadData(y.(buffer.VectorisedView)) })
	stateSourceObject.LoadValue(3, new(int64), func(y interface{}) { p.loadReceivedAt(y.(int64)) })
}

func (e *endpoint) StateTypeName() string {
	return "pkg/tcpip/transport/icmp.endpoint"
}

func (e *endpoint) StateFields() []string {
	return []string{
		"TransportEndpointInfo",
		"DefaultSocketOptionsHandler",
		"waiterQueue",
		"uniqueID",
		"rcvReady",
		"rcvList",
		"rcvBufSize",
		"rcvClosed",
		"shutdownFlags",
		"state",
		"ttl",
		"owner",
		"ops",
		"frozen",
	}
}

// +checklocksignore
func (e *endpoint) StateSave(stateSinkObject state.Sink) {
	e.beforeSave()
	stateSinkObject.Save(0, &e.TransportEndpointInfo)
	stateSinkObject.Save(1, &e.DefaultSocketOptionsHandler)
	stateSinkObject.Save(2, &e.waiterQueue)
	stateSinkObject.Save(3, &e.uniqueID)
	stateSinkObject.Save(4, &e.rcvReady)
	stateSinkObject.Save(5, &e.rcvList)
	stateSinkObject.Save(6, &e.rcvBufSize)
	stateSinkObject.Save(7, &e.rcvClosed)
	stateSinkObject.Save(8, &e.shutdownFlags)
	stateSinkObject.Save(9, &e.state)
	stateSinkObject.Save(10, &e.ttl)
	stateSinkObject.Save(11, &e.owner)
	stateSinkObject.Save(12, &e.ops)
	stateSinkObject.Save(13, &e.frozen)
}

// +checklocksignore
func (e *endpoint) StateLoad(stateSourceObject state.Source) {
	stateSourceObject.Load(0, &e.TransportEndpointInfo)
	stateSourceObject.Load(1, &e.DefaultSocketOptionsHandler)
	stateSourceObject.Load(2, &e.waiterQueue)
	stateSourceObject.Load(3, &e.uniqueID)
	stateSourceObject.Load(4, &e.rcvReady)
	stateSourceObject.Load(5, &e.rcvList)
	stateSourceObject.Load(6, &e.rcvBufSize)
	stateSourceObject.Load(7, &e.rcvClosed)
	stateSourceObject.Load(8, &e.shutdownFlags)
	stateSourceObject.Load(9, &e.state)
	stateSourceObject.Load(10, &e.ttl)
	stateSourceObject.Load(11, &e.owner)
	stateSourceObject.Load(12, &e.ops)
	stateSourceObject.Load(13, &e.frozen)
	stateSourceObject.AfterLoad(e.afterLoad)
}

func (l *icmpPacketList) StateTypeName() string {
	return "pkg/tcpip/transport/icmp.icmpPacketList"
}

func (l *icmpPacketList) StateFields() []string {
	return []string{
		"head",
		"tail",
	}
}

func (l *icmpPacketList) beforeSave() {}

// +checklocksignore
func (l *icmpPacketList) StateSave(stateSinkObject state.Sink) {
	l.beforeSave()
	stateSinkObject.Save(0, &l.head)
	stateSinkObject.Save(1, &l.tail)
}

func (l *icmpPacketList) afterLoad() {}

// +checklocksignore
func (l *icmpPacketList) StateLoad(stateSourceObject state.Source) {
	stateSourceObject.Load(0, &l.head)
	stateSourceObject.Load(1, &l.tail)
}

func (e *icmpPacketEntry) StateTypeName() string {
	return "pkg/tcpip/transport/icmp.icmpPacketEntry"
}

func (e *icmpPacketEntry) StateFields() []string {
	return []string{
		"next",
		"prev",
	}
}

func (e *icmpPacketEntry) beforeSave() {}

// +checklocksignore
func (e *icmpPacketEntry) StateSave(stateSinkObject state.Sink) {
	e.beforeSave()
	stateSinkObject.Save(0, &e.next)
	stateSinkObject.Save(1, &e.prev)
}

func (e *icmpPacketEntry) afterLoad() {}

// +checklocksignore
func (e *icmpPacketEntry) StateLoad(stateSourceObject state.Source) {
	stateSourceObject.Load(0, &e.next)
	stateSourceObject.Load(1, &e.prev)
}

func init() {
	state.Register((*icmpPacket)(nil))
	state.Register((*endpoint)(nil))
	state.Register((*icmpPacketList)(nil))
	state.Register((*icmpPacketEntry)(nil))
}
