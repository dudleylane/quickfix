package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;

public class FieldTests {
    public void testStringField() {
        StringField field = new StringField(55, "LNUX");
        Check.equal(55, field.getTag(), "tag");
        Check.equal("LNUX", field.getValue(), "value");
        Check.equal("55=LNUX\u0001", field.getFixString(), "FIX string");
        field.setValue("IBM");
        Check.equal("IBM", field.getString(), "after setValue");
    }

    public void testTypedFields() {
        Check.equal(100, new IntField(38, 100).getValue(), "int");
        Check.equal(12.5, new DoubleField(44, 12.5).getValue(), "double");
        Check.isTrue(new BoolField(43, true).getValue(), "bool");
        Check.equal('1', new CharField(54, '1').getValue(), "char");
    }

    public void testABadValueIsTheEnginesIncorrectDataFormat() {
        IntField field = new IntField(38);
        field.setString("not a number");
        IncorrectDataFormat e = Check.raises(IncorrectDataFormat.class, field::getValue);
        Check.equal(38, e.getField(), "the field it names");
        Check.isTrue(e instanceof FIXException, "a FIXException");
        Check.isTrue(e.getMessage().contains("38"), "the message names the tag: " + e.getMessage());
    }
}
